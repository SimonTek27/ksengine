#pragma once
/**
 * Minimal JSON value + reader/writer (Qt-free, header-only).
 *
 * The Qt-free runtime had no JSON support at all, and the installed layout
 * moved to JSON files (system/cfg/ksengine.json, user/<player>/*.json), so
 * this is the one place that understands them.
 *
 * Deliberate properties:
 *   - header-only inline code, no static state: ksengine.dll and every
 *     executable including this file each get their own copy and none of
 *     them shares mutable data (the discipline KSENGINE_API exists to
 *     enforce for types, applied to free functions too);
 *   - object members keep insertion order, so a file that is read back and
 *     written out again is stable instead of being reshuffled by a hash map;
 *   - UTF-8 in, UTF-8 out, \uXXXX (incl. surrogate pairs) decoded on input;
 *   - syntax errors are reported with a byte offset; no schema checking —
 *     callers decide which keys they accept (see EngineSettings::merge).
 *
 * Scope: what settings/data files need — objects, arrays, strings, numbers,
 * booleans, null. Not a general-purpose DOM.
 */
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace ks {
namespace engine {
namespace json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;

    static Value null() { return Value(); }
    static Value boolean(bool b) { Value v; v.m_type = Type::Bool; v.m_bool = b; return v; }
    static Value number(double d) { Value v; v.m_type = Type::Number; v.m_num = d; return v; }
    static Value string(const std::string& s) { Value v; v.m_type = Type::String; v.m_str = s; return v; }
    static Value array() { Value v; v.m_type = Type::Array; return v; }
    static Value object() { Value v; v.m_type = Type::Object; return v; }

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }
    bool isBool() const { return m_type == Type::Bool; }
    bool isNumber() const { return m_type == Type::Number; }
    bool isString() const { return m_type == Type::String; }
    bool isArray() const { return m_type == Type::Array; }
    bool isObject() const { return m_type == Type::Object; }

    // --- scalars -----------------------------------------------------------
    bool asBool(bool def = false) const { return m_type == Type::Bool ? m_bool : def; }
    double asNumber(double def = 0.0) const { return m_type == Type::Number ? m_num : def; }
    int asInt(int def = 0) const {
        return m_type == Type::Number ? static_cast<int>(std::llround(m_num)) : def;
    }
    std::string asString(const std::string& def = std::string()) const {
        return m_type == Type::String ? m_str : def;
    }

    // --- containers --------------------------------------------------------
    /** Element count of an array/object (0 for scalars). For an object this
     *  is the member count, matching keys().size(). */
    size_t size() const {
        return (m_type == Type::Array || m_type == Type::Object) ? m_items.size() : 0;
    }
    /** i-th array element / object value; nullptr when out of range or when
     *  this is a scalar. */
    const Value* item(size_t i) const {
        if ((m_type != Type::Array && m_type != Type::Object) || i >= m_items.size())
            return nullptr;
        return &m_items[i];
    }
    /** Object member keys in insertion order; empty for everything else. */
    const std::vector<std::string>& keys() const { return m_keys; }
    /** Object member by key; nullptr when this is not an object or the key
     *  is absent. */
    const Value* find(const std::string& key) const {
        if (m_type != Type::Object) return nullptr;
        for (size_t i = 0; i < m_keys.size(); ++i)
            if (m_keys[i] == key) return &m_items[i];
        return nullptr;
    }

    // --- typed member access (scalar members only; anything else -> def) ---
    double numberAt(const std::string& key, double def) const {
        const Value* v = find(key);
        return (v && v->m_type == Type::Number) ? v->m_num : def;
    }
    int intAt(const std::string& key, int def) const {
        const Value* v = find(key);
        return (v && v->m_type == Type::Number) ? v->asInt(def) : def;
    }
    bool boolAt(const std::string& key, bool def) const {
        const Value* v = find(key);
        return (v && v->m_type == Type::Bool) ? v->m_bool : def;
    }
    std::string stringAt(const std::string& key, const std::string& def = std::string()) const {
        const Value* v = find(key);
        return (v && v->m_type == Type::String) ? v->m_str : def;
    }

    // --- building ----------------------------------------------------------
    /** Append to an array (a scalar/null receiver becomes an array). */
    Value& append(Value v) {
        Value val = std::move(v); // copy-out first: v may alias *this
        if (m_type != Type::Array) *this = array();
        m_items.push_back(std::move(val));
        return m_items.back();
    }
    /** Insert or replace an object member (a scalar/null receiver becomes an
     *  object). The existing member keeps its position. */
    Value& set(const std::string& key, Value v) {
        Value val = std::move(v);
        if (m_type != Type::Object) *this = object();
        for (size_t i = 0; i < m_keys.size(); ++i) {
            if (m_keys[i] == key) {
                m_items[i] = std::move(val);
                return m_items[i];
            }
        }
        m_keys.push_back(key);
        m_items.push_back(std::move(val));
        return m_items.back();
    }

private:
    Type m_type = Type::Null;
    bool m_bool = false;
    double m_num = 0.0;
    std::string m_str;
    /** Object member keys, parallel to m_items (array elements live there
     *  too — for an object, m_items[i] is the value of m_keys[i]). */
    std::vector<std::string> m_keys;
    std::vector<Value> m_items;
};

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

namespace detail {

/** Recursive-descent parser. `error` (optional) receives "<what> at offset N"
 *  for the first syntax problem; the offset is into the input text. */
class Parser {
public:
    Parser(const char* text, size_t size, std::string* error)
        : m_p(text), m_end(text + size), m_base(text), m_err(error) {}

    bool parseDocument(Value& out) {
        skipWs();
        if (!parseValue(out, 0)) return false;
        skipWs();
        if (m_p != m_end) return fail("trailing data after the value");
        return true;
    }

private:
    const char* m_p;
    const char* m_end;
    const char* m_base;
    std::string* m_err;

    bool fail(const char* what) {
        if (m_err && m_err->empty()) {
            *m_err = std::string(what) + " at offset " +
                     std::to_string(static_cast<long long>(m_p - m_base));
        }
        return false;
    }
    bool eof() const { return m_p >= m_end; }
    void skipWs() {
        while (m_p < m_end && (*m_p == ' ' || *m_p == '\t' || *m_p == '\n' || *m_p == '\r'))
            ++m_p;
    }
    /** Match a bare literal (true/false/null) as a whole word. */
    bool literal(const char* s) {
        const size_t n = std::strlen(s);
        if (static_cast<size_t>(m_end - m_p) < n) return false;
        if (std::memcmp(m_p, s, n) != 0) return false;
        m_p += n;
        return true;
    }

    bool parseValue(Value& out, int depth) {
        if (depth > 64) return fail("nesting too deep");
        if (eof()) return fail("unexpected end of input");
        switch (*m_p) {
        case '{': return parseObject(out, depth);
        case '[': return parseArray(out, depth);
        case '"': {
            std::string s;
            if (!parseString(s)) return false;
            out = Value::string(s);
            return true;
        }
        case 't':
            if (!literal("true")) return fail("invalid literal");
            out = Value::boolean(true);
            return true;
        case 'f':
            if (!literal("false")) return fail("invalid literal");
            out = Value::boolean(false);
            return true;
        case 'n':
            if (!literal("null")) return fail("invalid literal");
            out = Value::null();
            return true;
        default:
            return parseNumber(out);
        }
    }

    bool parseObject(Value& out, int depth) {
        ++m_p; // '{'
        out = Value::object();
        skipWs();
        if (!eof() && *m_p == '}') {
            ++m_p;
            return true;
        }
        for (;;) {
            skipWs();
            if (eof() || *m_p != '"') return fail("expected a member key");
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (eof() || *m_p != ':') return fail("expected ':'");
            ++m_p;
            skipWs();
            Value v;
            if (!parseValue(v, depth + 1)) return false;
            out.set(key, std::move(v));
            skipWs();
            if (eof()) return fail("unterminated object");
            if (*m_p == ',') {
                ++m_p;
                continue;
            }
            if (*m_p == '}') {
                ++m_p;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool parseArray(Value& out, int depth) {
        ++m_p; // '['
        out = Value::array();
        skipWs();
        if (!eof() && *m_p == ']') {
            ++m_p;
            return true;
        }
        for (;;) {
            skipWs();
            Value v;
            if (!parseValue(v, depth + 1)) return false;
            out.append(std::move(v));
            skipWs();
            if (eof()) return fail("unterminated array");
            if (*m_p == ',') {
                ++m_p;
                continue;
            }
            if (*m_p == ']') {
                ++m_p;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    /** Reads exactly four hex digits of a \uXXXX escape into `out`. */
    bool parseHex4(unsigned& out) {
        if (m_end - m_p < 4) return fail("truncated \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = m_p[i];
            unsigned d;
            if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
            else return fail("invalid hex digit in \\u escape");
            out = (out << 4) | d;
        }
        m_p += 4;
        return true;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    /** String body after the opening quote (consumes the closing quote). */
    bool parseString(std::string& out) {
        ++m_p; // '"'
        out.clear();
        for (;;) {
            if (eof()) return fail("unterminated string");
            const unsigned char c = static_cast<unsigned char>(*m_p++);
            if (c == '"') return true;
            if (c < 0x20) return fail("raw control character in string");
            if (c != '\\') {
                out += static_cast<char>(c);
                continue;
            }
            if (eof()) return fail("unterminated escape");
            const char e = *m_p++;
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = 0;
                if (!parseHex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) { // high surrogate: needs a low one
                    if (m_end - m_p >= 6 && m_p[0] == '\\' && m_p[1] == 'u') {
                        const char* save = m_p;
                        m_p += 2;
                        unsigned low = 0;
                        if (!parseHex4(low) || low < 0xDC00 || low > 0xDFFF) {
                            m_p = save;
                            return fail("invalid low surrogate");
                        }
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                    } else {
                        return fail("unpaired surrogate");
                    }
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return fail("unpaired surrogate");
                }
                appendUtf8(out, cp);
                break;
            }
            default:
                return fail("invalid escape");
            }
        }
    }

    bool parseNumber(Value& out) {
        const char* start = m_p;
        while (m_p < m_end) {
            const char c = *m_p;
            const bool num = (c >= '0' && c <= '9') || c == '-' || c == '+' ||
                             c == '.' || c == 'e' || c == 'E';
            if (!num) break;
            ++m_p;
        }
        if (m_p == start) return fail("invalid value");
        const std::string token(start, m_p);
        char* endp = nullptr;
        const double d = std::strtod(token.c_str(), &endp);
        if (endp != token.c_str() + token.size() || !std::isfinite(d))
            return fail("invalid number");
        out = Value::number(d);
        return true;
    }
};

} // namespace detail

/**
 * Parse one JSON document. Returns false on malformed input (nothing in
 * `out` is touched then); `error`, when given, describes the first problem.
 */
inline bool parse(const std::string& text, Value& out, std::string* error = nullptr) {
    if (error) error->clear();
    detail::Parser parser(text.data(), text.size(), error);
    Value v;
    if (!parser.parseDocument(v)) return false;
    out = std::move(v);
    return true;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

namespace detail {

inline void writeEscaped(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                out += static_cast<char>(c); // bytes >= 0x80 pass through (UTF-8)
            }
        }
    }
    out += '"';
}

/** Shortest representation that reads back as exactly the same double;
 *  integral values print without a decimal point (5, not 5.0). */
inline std::string numberToString(double d) {
    if (!std::isfinite(d)) return "null"; // JSON has no NaN/Inf
    char buf[40];
    if (d == static_cast<double>(static_cast<long long>(d)) &&
        d >= -9.0e14 && d <= 9.0e14) {
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
        return buf;
    }
    for (int prec = 15; prec <= 17; ++prec) {
        std::snprintf(buf, sizeof(buf), "%.*g", prec, d);
        if (std::strtod(buf, nullptr) == d) break;
    }
    return buf;
}

inline void dumpValue(const Value& v, std::string& out, int indent, int depth) {
    const int child = depth + 1;
    const auto newline = [&](int d) {
        if (indent > 0) {
            out += '\n';
            out.append(static_cast<size_t>(indent * d), ' ');
        }
    };
    switch (v.type()) {
    case Value::Type::Null: out += "null"; break;
    case Value::Type::Bool: out += v.asBool() ? "true" : "false"; break;
    case Value::Type::Number: out += numberToString(v.asNumber()); break;
    case Value::Type::String: writeEscaped(v.asString(), out); break;
    case Value::Type::Array:
        if (v.size() == 0) {
            out += "[]";
            break;
        }
        out += '[';
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) out += ',';
            newline(child);
            dumpValue(*v.item(i), out, indent, child);
        }
        newline(depth);
        out += ']';
        break;
    case Value::Type::Object:
        if (v.size() == 0) {
            out += "{}";
            break;
        }
        out += '{';
        for (size_t i = 0; i < v.keys().size(); ++i) {
            if (i) out += ',';
            newline(child);
            writeEscaped(v.keys()[i], out);
            out += (indent > 0) ? ": " : ":";
            dumpValue(*v.item(i), out, indent, child);
        }
        newline(depth);
        out += '}';
        break;
    }
}

} // namespace detail

/**
 * Serialise to text. indent > 0 pretty-prints with that many spaces per
 * level and a trailing newline; indent <= 0 emits the compact single-line
 * form. Either way the result parses back to an equal value.
 */
inline std::string dump(const Value& v, int indent = 2) {
    std::string out;
    detail::dumpValue(v, out, indent, 0);
    if (indent > 0) out += '\n';
    return out;
}

} // namespace json
} // namespace engine
} // namespace ks
