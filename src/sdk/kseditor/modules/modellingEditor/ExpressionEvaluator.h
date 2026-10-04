#pragma once

// Lightweight scalar expression evaluator for parameter wiring (Softimage
// "Expression" style). Evaluates a single expression over one driver variable
// `x`. Used by WireParameterSystem to drive a parameter by an arbitrary math
// expression rather than a plain affine map.
//
// Grammar (recursive descent):
//   expr   := term (('+' | '-') term)*
//   term   := unary (('*' | '/' | '%') unary)*
//   unary  := ('-' | '+') unary
//           | power
//   power  := primary ('^' unary)?
//   primary:= number | 'x' | 'pi' | 'e'
//           | ident '(' args ')' | '(' expr ')'
//   args   := expr (',' expr)*
//
// Supported functions: abs, sign, floor, ceil, round, trunc, fract,
// sqrt, sq, cbrt, exp, ln, log (base-10), log2, sin, cos, tan,
// asin, acos, atan, atan2(y,x), sinh, cosh, tanh, pow(a,b), fmod(a,b),
// min(a,b), max(a,b), clamp(v,lo,hi), mix(a,b,t), smoothstep(lo,hi,v).

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ks {
namespace expr {

class ExpressionEvaluator
{
public:
    // Evaluates `expression` over a driver value `x`. On success returns the
    // result and sets ok to true; on a parse/evaluation error returns 0.0 and
    // sets ok to false (lastError() holds a human-readable message).
    static double evaluate(const QString& expression, double x, bool* ok = nullptr)
    {
        ExpressionEvaluator ev(expression, x);
        ev.skipSpace();
        double result = 0.0;
        ev.parseExpr(result, ok);
        if (ev.m_ok && !ev.atEnd()) {
            ev.m_error = QStringLiteral("Unexpected trailing input at position %1").arg(ev.m_pos);
            ev.m_ok = false;
        }
        const bool success = ev.m_ok;
        if (ok) *ok = success;
        return success ? result : 0.0;
    }

    static QString lastError() { return lastErrorStorage(); }

private:
    static QString& lastErrorStorage() {
        static QString e;
        return e;
    }

    ExpressionEvaluator(const QString& src, double x)
        : m_src(src), m_x(x), m_pos(0), m_error(QString()), m_ok(true) {}

    bool atEnd() const { return m_pos >= m_src.size(); }
    QChar peek() const { return atEnd() ? QChar() : m_src.at(m_pos); }

    void skipSpace() {
        while (!atEnd() && m_src.at(m_pos).isSpace()) ++m_pos;
    }

    double parseExpr(double& out, bool* okFlag)
    {
        if (!m_ok) { if (okFlag) *okFlag = false; return 0.0; }
        skipSpace();
        double left = parseTerm(okFlag);
        if (!m_ok) { if (okFlag) *okFlag = false; return 0.0; }
        for (;;) {
            skipSpace();
            if (atEnd()) break;
            const QChar c = peek();
            if (c == '+') { ++m_pos; double r = parseTerm(okFlag); if (!m_ok) break; left = left + r; }
            else if (c == '-') { ++m_pos; double r = parseTerm(okFlag); if (!m_ok) break; left = left - r; }
            else break;
        }
        return (out = left);
    }

    double parseTerm(bool* okFlag)
    {
        if (!m_ok) { if (okFlag) *okFlag = false; return 0.0; }
        skipSpace();
        double left = parseUnary(okFlag);
        if (!m_ok) { if (okFlag) *okFlag = false; return 0.0; }
        for (;;) {
            skipSpace();
            if (atEnd()) break;
            const QChar c = peek();
            if (c == '*') { ++m_pos; double r = parseUnary(okFlag); if (!m_ok) break; left = left * r; }
            else if (c == '/') { ++m_pos; double r = parseUnary(okFlag); if (!m_ok) break; if (r == 0.0) { fail(QStringLiteral("Division by zero")); if (okFlag) *okFlag = false; return 0.0; } left = left / r; }
            else if (c == '%') { ++m_pos; double r = parseUnary(okFlag); if (!m_ok) break; if (r == 0.0) { fail(QStringLiteral("Modulo by zero")); if (okFlag) *okFlag = false; return 0.0; } left = std::fmod(left, r); }
            else break;
        }
        return left;
    }

    double parseUnary(bool* okFlag)
    {
        if (!m_ok) { if (okFlag) *okFlag = false; return 0.0; }
        skipSpace();
        if (peek() == '-') { ++m_pos; return -parseUnary(okFlag); }
        if (peek() == '+') { ++m_pos; return parseUnary(okFlag); }
        return parsePower(okFlag);
    }

    double parsePower(bool* okFlag)
    {
        if (!m_ok) { if (okFlag) *okFlag = false; return 0.0; }
        skipSpace();
        double base = parsePrimary(okFlag);
        if (!m_ok) { if (okFlag) *okFlag = false; return 0.0; }
        skipSpace();
        if (peek() == '^') {
            ++m_pos;
            double exp = parseUnary(okFlag);
            if (!m_ok) return 0.0;
            // x^0.5 etc. via pow; negative bases resolve as pow would.
            return std::pow(base, exp);
        }
        return base;
    }

    double parsePrimary(bool* okFlag)
    {
        if (!m_ok) { if (okFlag) *okFlag = false; return 0.0; }
        skipSpace();
        if (atEnd()) { fail(QStringLiteral("Unexpected end of expression")); if (okFlag) *okFlag = false; return 0.0; }

        const QChar c = peek();
        if (c == '(') {
            ++m_pos;
            double v = 0.0;
            parseExpr(v, okFlag);
            if (!m_ok) { if (okFlag) *okFlag = false; return 0.0; }
            skipSpace();
            if (peek() != ')') { fail(QStringLiteral("Missing closing parenthesis")); if (okFlag) *okFlag = false; return 0.0; }
            ++m_pos;
            return v;
        }
        if (c.isDigit() || c == '.') {
            return parseNumber(okFlag);
        }
        if (c.isLetter() || c == '_') {
            return parseIdentifier(okFlag);
        }
        fail(QStringLiteral("Unexpected character '%1'").arg(c));
        if (okFlag) *okFlag = false;
        return 0.0;
    }

    double parseNumber(bool* okFlag)
    {
        const int start = m_pos;
        bool isFloat = false;
        int digits = 0;
        while (!atEnd() && peek().isDigit()) { ++m_pos; ++digits; }
        if (!atEnd() && peek() == '.') {
            isFloat = true;
            ++m_pos;
            while (!atEnd() && peek().isDigit()) { ++m_pos; ++digits; }
        }
        if (digits == 0) { fail(QStringLiteral("Invalid number")); if (okFlag) *okFlag = false; return 0.0; }
        // Optional exponent
        if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
            const int saved = m_pos;
            ++m_pos;
            if (!atEnd() && (peek() == '+' || peek() == '-')) ++m_pos;
            const int expStart = m_pos;
            while (!atEnd() && peek().isDigit()) ++m_pos;
            if (m_pos == expStart) { m_pos = saved; } // no exponent digits → roll back
            else isFloat = true;
            (void)isFloat;
        }
        bool okNum = false;
        double v = m_src.mid(start, m_pos - start).toDouble(&okNum);
        if (!okNum) { fail(QStringLiteral("Invalid number '%1'").arg(m_src.mid(start, m_pos - start))); if (okFlag) *okFlag = false; return 0.0; }
        return v;
    }

    double parseIdentifier(bool* okFlag)
    {
        const int start = m_pos;
        while (!atEnd() && (peek().isLetterOrNumber() || peek() == '_')) ++m_pos;
        const QString name = m_src.mid(start, m_pos - start);

        if (name == QLatin1String("x")) return m_x;
        if (name == QLatin1String("pi")) return 3.14159265358979323846;
        if (name == QLatin1String("e")) return 2.71828182845904523536;
        if (name == QLatin1String("true")) return 1.0;
        if (name == QLatin1String("false")) return 0.0;

        skipSpace();
        if (peek() == '(') {
            ++m_pos;
            if (!name.isEmpty() && !m_funcs.contains(name)) {
                fail(QStringLiteral("Unknown function '%1'").arg(name));
                if (okFlag) *okFlag = false;
                return 0.0;
            }
            return parseFunction(name, okFlag);
        }
        fail(QStringLiteral("Unknown identifier '%1'").arg(name));
        if (okFlag) *okFlag = false;
        return 0.0;
    }

    double parseFunction(const QString& name, bool* okFlag)
    {
        // Parse comma-separated args.
        QVector<double> args;
        skipSpace();
        if (peek() == ')') {
            ++m_pos;
        } else {
            for (;;) {
                double a = 0.0;
                skipSpace();
                if (name == QLatin1String("smoothstep") || name == QLatin1String("mix") ||
                    name == QLatin1String("clamp") || name == QLatin1String("min") ||
                    name == QLatin1String("max") || name == QLatin1String("pow") ||
                    name == QLatin1String("fmod") || name == QLatin1String("atan2")) {
                    a = parseArgRaw(okFlag);
                } else {
                    parseExpr(a, okFlag);
                }
                if (!m_ok) { if (okFlag) *okFlag = false; return 0.0; }
                args.append(a);
                skipSpace();
                if (peek() == ',') { ++m_pos; continue; }
                break;
            }
            skipSpace();
            if (peek() != ')') { fail(QStringLiteral("Missing ')' after arguments")); if (okFlag) *okFlag = false; return 0.0; }
            ++m_pos;
        }

        const int n = args.size();
        const auto need = [&](int k) -> bool {
            if (n >= k) return true;
            fail(QStringLiteral("Function '%1' expects %2 argument(s)").arg(name).arg(k));
            if (okFlag) *okFlag = false;
            return false;
        };

        if (name == QLatin1String("abs")) { if (need(1)) return std::fabs(args[0]); }
        else if (name == QLatin1String("sign")) { if (need(1)) return args[0] > 0.0 ? 1.0 : (args[0] < 0.0 ? -1.0 : 0.0); }
        else if (name == QLatin1String("floor")) { if (need(1)) return std::floor(args[0]); }
        else if (name == QLatin1String("ceil")) { if (need(1)) return std::ceil(args[0]); }
        else if (name == QLatin1String("round")) { if (need(1)) return std::round(args[0]); }
        else if (name == QLatin1String("trunc")) { if (need(1)) return std::trunc(args[0]); }
        else if (name == QLatin1String("fract")) { if (need(1)) { double t; t = std::modf(args[0], &t); return t; } }
        else if (name == QLatin1String("sqrt")) { if (need(1)) return std::sqrt(args[0]); }
        else if (name == QLatin1String("sq")) { if (need(1)) return args[0] * args[0]; }
        else if (name == QLatin1String("cbrt")) { if (need(1)) return std::cbrt(args[0]); }
        else if (name == QLatin1String("exp")) { if (need(1)) return std::exp(args[0]); }
        else if (name == QLatin1String("ln")) { if (need(1)) return std::log(args[0]); }
        else if (name == QLatin1String("log")) { if (need(1)) return std::log10(args[0]); }
        else if (name == QLatin1String("log2")) { if (need(1)) return std::log2(args[0]); }
        else if (name == QLatin1String("sin")) { if (need(1)) return std::sin(args[0]); }
        else if (name == QLatin1String("cos")) { if (need(1)) return std::cos(args[0]); }
        else if (name == QLatin1String("tan")) { if (need(1)) return std::tan(args[0]); }
        else if (name == QLatin1String("asin")) { if (need(1)) return std::asin(args[0]); }
        else if (name == QLatin1String("acos")) { if (need(1)) return std::acos(args[0]); }
        else if (name == QLatin1String("atan")) { if (need(1)) return std::atan(args[0]); }
        else if (name == QLatin1String("atan2")) { if (need(2)) return std::atan2(args[0], args[1]); }
        else if (name == QLatin1String("sinh")) { if (need(1)) return std::sinh(args[0]); }
        else if (name == QLatin1String("cosh")) { if (need(1)) return std::cosh(args[0]); }
        else if (name == QLatin1String("tanh")) { if (need(1)) return std::tanh(args[0]); }
        else if (name == QLatin1String("pow")) { if (need(2)) return std::pow(args[0], args[1]); }
        else if (name == QLatin1String("fmod")) { if (need(2)) return args[1] == 0.0 ? 0.0 : std::fmod(args[0], args[1]); }
        else if (name == QLatin1String("min")) { if (need(2)) return qMin(args[0], args[1]); }
        else if (name == QLatin1String("max")) { if (need(2)) return qMax(args[0], args[1]); }
        else if (name == QLatin1String("clamp")) {
            if (need(3)) { const double lo = qMin(args[1], args[2]), hi = qMax(args[1], args[2]); return args[0] < lo ? lo : (args[0] > hi ? hi : args[0]); }
        }
        else if (name == QLatin1String("mix")) { if (need(3)) return args[0] * (1.0 - args[2]) + args[1] * args[2]; }
        else if (name == QLatin1String("smoothstep")) {
            if (need(3)) {
                const double t = (args[2] - args[0]) / (args[1] - args[0]);
                const double s = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
                return s * s * (3.0 - 2.0 * s);
            }
        }
        else {
            fail(QStringLiteral("Unknown function '%1'").arg(name));
            if (okFlag) *okFlag = false;
            return 0.0;
        }
        return 0.0;
    }

    // Used for 2+ arg functions so that comma-separated primitive args parse
    // without crashing on the trailing comma.
    double parseArgRaw(bool* okFlag)
    {
        skipSpace();
        if (peek() == '(') { ++m_pos; double v; parseExpr(v, okFlag); skipSpace(); if (peek() == ')') ++m_pos; return v; }
        if (peek() == '-' || peek() == '+') return parseUnary(okFlag);
        if (peek().isDigit() || peek() == '.') return parseNumber(okFlag);
        return parsePrimary(okFlag);
    }

    void fail(const QString& msg)
    {
        if (m_ok) {
            m_ok = false;
            m_error = msg;
            lastErrorStorage() = msg;
        }
    }

    QString m_src;
    double m_x;
    int m_pos;
    QString m_error;
    bool m_ok;
    QStringList m_funcs = {
        QStringLiteral("abs"), QStringLiteral("sign"), QStringLiteral("floor"),
        QStringLiteral("ceil"), QStringLiteral("round"), QStringLiteral("trunc"),
        QStringLiteral("fract"), QStringLiteral("sqrt"), QStringLiteral("sq"),
        QStringLiteral("cbrt"), QStringLiteral("exp"), QStringLiteral("ln"),
        QStringLiteral("log"), QStringLiteral("log2"), QStringLiteral("sin"),
        QStringLiteral("cos"), QStringLiteral("tan"), QStringLiteral("asin"),
        QStringLiteral("acos"), QStringLiteral("atan"), QStringLiteral("atan2"),
        QStringLiteral("sinh"), QStringLiteral("cosh"), QStringLiteral("tanh"),
        QStringLiteral("pow"), QStringLiteral("fmod"), QStringLiteral("min"),
        QStringLiteral("max"), QStringLiteral("clamp"), QStringLiteral("mix"),
        QStringLiteral("smoothstep")
    };
};

} // namespace expr
} // namespace ks