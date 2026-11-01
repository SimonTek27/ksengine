#include "Json.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ks {

namespace {

bool isJsonSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

int hexDigitValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void appendUtf8(std::string& out, unsigned int codePoint) {
    if (codePoint < 0x80) {
        out += static_cast<char>(codePoint);
    } else if (codePoint < 0x800) {
        out += static_cast<char>(0xC0 | (codePoint >> 6));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else if (codePoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codePoint >> 12));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codePoint >> 18));
        out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
}

void appendEscaped(std::string& out, const std::string& text) {
    out += '"';
    for (char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned int>(static_cast<unsigned char>(c)));
                out += buffer;
            } else {
                out += c;
            }
            break;
        }
    }
    out += '"';
}

class Parser {
public:
    explicit Parser(const std::string& text) : m_text(text) {}

    bool run(Json& out, std::string& error) {
        if (m_text.size() >= 3 && static_cast<unsigned char>(m_text[0]) == 0xEF &&
            static_cast<unsigned char>(m_text[1]) == 0xBB &&
            static_cast<unsigned char>(m_text[2]) == 0xBF) {
            m_pos = 3;
        }
        skipWhitespace();
        if (m_pos >= m_text.size()) return fail(m_pos, "unexpected end of input");
        if (!parseValue(out)) {
            error = m_error;
            return false;
        }
        skipWhitespace();
        if (m_pos != m_text.size()) return fail(m_pos, "unexpected trailing characters");
        return true;
    }

private:
    bool fail(std::size_t pos, const std::string& message) {
        m_error = "JSON parse error at position " + std::to_string(pos) + ": " + message;
        return false;
    }

    void skipWhitespace() {
        while (m_pos < m_text.size() && isJsonSpace(m_text[m_pos])) ++m_pos;
    }

    bool parseValue(Json& out) {
        if (m_pos >= m_text.size()) return fail(m_pos, "unexpected end of input");
        switch (m_text[m_pos]) {
        case '{': return parseObject(out);
        case '[': return parseArray(out);
        case '"': {
            std::string text;
            if (!parseString(text)) return false;
            out = Json(text);
            return true;
        }
        case 't': return parseLiteral("true", out, Json(true));
        case 'f': return parseLiteral("false", out, Json(false));
        case 'n': return parseLiteral("null", out, Json());
        default: return parseNumber(out);
        }
    }

    bool parseLiteral(const char* literal, Json& out, const Json& value) {
        std::size_t length = std::strlen(literal);
        if (m_text.compare(m_pos, length, literal) != 0) {
            return fail(m_pos, std::string("invalid literal, expected '") + literal + "'");
        }
        m_pos += length;
        out = value;
        return true;
    }

    bool parseHex4(unsigned int& value) {
        if (m_pos + 4 > m_text.size()) return fail(m_pos, "incomplete \\u escape");
        value = 0;
        for (int i = 0; i < 4; ++i) {
            int digit = hexDigitValue(m_text[m_pos]);
            if (digit < 0) return fail(m_pos, "invalid hex digit in \\u escape");
            value = (value << 4) | static_cast<unsigned int>(digit);
            ++m_pos;
        }
        return true;
    }

    bool parseString(std::string& out) {
        out.clear();
        ++m_pos;
        while (m_pos < m_text.size()) {
            char c = m_text[m_pos];
            if (c == '"') {
                ++m_pos;
                return true;
            }
            if (c == '\\') {
                ++m_pos;
                if (m_pos >= m_text.size()) return fail(m_pos, "unterminated escape sequence");
                char escape = m_text[m_pos];
                ++m_pos;
                switch (escape) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned int codePoint = 0;
                    if (!parseHex4(codePoint)) return false;
                    if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
                        unsigned int low = 0;
                        std::size_t saved = m_pos;
                        bool paired = false;
                        if (m_pos + 1 < m_text.size() && m_text[m_pos] == '\\' && m_text[m_pos + 1] == 'u') {
                            m_pos += 2;
                            if (!parseHex4(low)) return false;
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                paired = true;
                            } else {
                                m_pos = saved;
                            }
                        }
                        if (paired) {
                            codePoint = 0x10000u + ((codePoint - 0xD800u) << 10) + (low - 0xDC00u);
                        } else {
                            codePoint = 0xFFFD;
                        }
                    } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
                        codePoint = 0xFFFD;
                    }
                    appendUtf8(out, codePoint);
                    break;
                }
                default:
                    return fail(m_pos - 1, std::string("invalid escape sequence '\\") + escape + "'");
                }
                continue;
            }
            out += c;
            ++m_pos;
        }
        return fail(m_pos, "unterminated string");
    }

    bool parseNumber(Json& out) {
        std::size_t start = m_pos;
        if (m_text[m_pos] == '-') ++m_pos;
        if (m_pos >= m_text.size() || !isDigit(m_text[m_pos])) return fail(m_pos, "invalid number");
        if (m_text[m_pos] == '0') {
            ++m_pos;
        } else {
            while (m_pos < m_text.size() && isDigit(m_text[m_pos])) ++m_pos;
        }
        bool isInteger = true;
        if (m_pos < m_text.size() && m_text[m_pos] == '.') {
            isInteger = false;
            ++m_pos;
            if (m_pos >= m_text.size() || !isDigit(m_text[m_pos])) return fail(m_pos, "invalid number");
            while (m_pos < m_text.size() && isDigit(m_text[m_pos])) ++m_pos;
        }
        if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
            isInteger = false;
            ++m_pos;
            if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-')) ++m_pos;
            if (m_pos >= m_text.size() || !isDigit(m_text[m_pos])) return fail(m_pos, "invalid number exponent");
            while (m_pos < m_text.size() && isDigit(m_text[m_pos])) ++m_pos;
        }

        std::string token = m_text.substr(start, m_pos - start);
        if (isInteger) {
            errno = 0;
            char* end = nullptr;
            long long value = std::strtoll(token.c_str(), &end, 10);
            if (errno != ERANGE && end && *end == '\0') {
                out = Json(value);
                return true;
            }
        }
        errno = 0;
        char* end = nullptr;
        double value = std::strtod(token.c_str(), &end);
        if (!end || *end != '\0') return fail(start, "invalid number");
        if (!std::isfinite(value)) return fail(start, "number out of range");
        out = Json(value);
        return true;
    }

    bool parseObject(Json& out) {
        ++m_pos;
        out = Json::object();
        skipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == '}') {
            ++m_pos;
            return true;
        }
        while (true) {
            skipWhitespace();
            if (m_pos >= m_text.size() || m_text[m_pos] != '"') {
                return fail(m_pos, "expected string key");
            }
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (m_pos >= m_text.size() || m_text[m_pos] != ':') return fail(m_pos, "expected ':'");
            ++m_pos;
            skipWhitespace();
            Json value;
            if (!parseValue(value)) return false;
            out.insert(key, value);
            skipWhitespace();
            if (m_pos >= m_text.size()) return fail(m_pos, "unterminated object");
            if (m_text[m_pos] == ',') {
                ++m_pos;
                continue;
            }
            if (m_text[m_pos] == '}') {
                ++m_pos;
                return true;
            }
            return fail(m_pos, "expected ',' or '}'");
        }
    }

    bool parseArray(Json& out) {
        ++m_pos;
        out = Json::array();
        skipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == ']') {
            ++m_pos;
            return true;
        }
        while (true) {
            skipWhitespace();
            Json value;
            if (!parseValue(value)) return false;
            out.append(value);
            skipWhitespace();
            if (m_pos >= m_text.size()) return fail(m_pos, "unterminated array");
            if (m_text[m_pos] == ',') {
                ++m_pos;
                continue;
            }
            if (m_text[m_pos] == ']') {
                ++m_pos;
                return true;
            }
            return fail(m_pos, "expected ',' or ']'");
        }
    }

    const std::string& m_text;
    std::size_t m_pos = 0;
    std::string m_error;
};

} // namespace

Json::Json() = default;

Json::Json(bool value) : m_type(Type::Bool), m_bool(value) {}

Json::Json(int value)
    : m_type(Type::Number), m_isInt(true), m_int(value), m_double(static_cast<double>(value)) {}

Json::Json(long value)
    : m_type(Type::Number), m_isInt(true), m_int(value), m_double(static_cast<double>(value)) {}

Json::Json(long long value)
    : m_type(Type::Number), m_isInt(true), m_int(value), m_double(static_cast<double>(value)) {}

Json::Json(double value) : m_type(Type::Number), m_double(value) {
    if (std::isfinite(value) && value == std::floor(value) &&
        value >= -9.007199254740992e15 && value <= 9.007199254740992e15) {
        m_int = static_cast<long long>(value);
    }
}

Json::Json(const char* value) : m_type(Type::String), m_string(value ? value : "") {}

Json::Json(const std::string& value) : m_type(Type::String), m_string(value) {}

Json::Json(UndefinedTag) : m_type(Type::Undefined) {}

Json Json::object() {
    Json value;
    value.m_type = Type::Object;
    return value;
}

Json Json::array() {
    Json value;
    value.m_type = Type::Array;
    return value;
}

const Json& Json::undefinedValue() {
    static const Json value(UndefinedTag{});
    return value;
}

bool Json::isEmpty() const {
    switch (m_type) {
    case Type::Undefined:
    case Type::Null: return true;
    case Type::String: return m_string.empty();
    case Type::Array: return m_array.empty();
    case Type::Object: return m_object.empty();
    default: return false;
    }
}

bool Json::toBool(bool defaultValue) const {
    return m_type == Type::Bool ? m_bool : defaultValue;
}

int Json::toInt(int defaultValue) const {
    if (m_type != Type::Number) return defaultValue;
    if (m_isInt) {
        if (m_int < -2147483648LL || m_int > 2147483647LL) return defaultValue;
        return static_cast<int>(m_int);
    }
    if (!std::isfinite(m_double) || m_double < -2147483648.0 || m_double > 2147483647.0) {
        return defaultValue;
    }
    return static_cast<int>(m_double);
}

long long Json::toInt64(long long defaultValue) const {
    if (m_type != Type::Number) return defaultValue;
    if (m_isInt) return m_int;
    if (!std::isfinite(m_double) || m_double < -9223372036854775808.0 ||
        m_double >= 9223372036854775808.0) {
        return defaultValue;
    }
    return static_cast<long long>(m_double);
}

double Json::toDouble(double defaultValue) const {
    if (m_type != Type::Number) return defaultValue;
    return m_isInt ? static_cast<double>(m_int) : m_double;
}

std::string Json::toString(const std::string& defaultValue) const {
    return m_type == Type::String ? m_string : defaultValue;
}

bool Json::contains(const std::string& key) const {
    if (m_type != Type::Object) return false;
    for (const auto& entry : m_object) {
        if (entry.first == key) return true;
    }
    return false;
}

Json Json::value(const std::string& key) const {
    return (*this)[key];
}

const Json& Json::operator[](const std::string& key) const {
    if (m_type == Type::Object) {
        for (const auto& entry : m_object) {
            if (entry.first == key) return entry.second;
        }
    }
    return undefinedValue();
}

Json& Json::operator[](const std::string& key) {
    if (m_type != Type::Object) {
        *this = object();
    }
    for (auto& entry : m_object) {
        if (entry.first == key) return entry.second;
    }
    m_object.emplace_back(key, Json());
    return m_object.back().second;
}

void Json::insert(const std::string& key, const Json& value) {
    if (m_type != Type::Object) {
        *this = object();
    }
    for (auto& entry : m_object) {
        if (entry.first == key) {
            entry.second = value;
            return;
        }
    }
    m_object.emplace_back(key, value);
}

void Json::remove(const std::string& key) {
    if (m_type != Type::Object) return;
    for (std::size_t i = 0; i < m_object.size(); ++i) {
        if (m_object[i].first == key) {
            m_object.erase(m_object.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

std::vector<std::string> Json::keys() const {
    std::vector<std::string> result;
    if (m_type != Type::Object) return result;
    result.reserve(m_object.size());
    for (const auto& entry : m_object) result.push_back(entry.first);
    return result;
}

int Json::size() const {
    switch (m_type) {
    case Type::Array: return static_cast<int>(m_array.size());
    case Type::Object: return static_cast<int>(m_object.size());
    default: return 0;
    }
}

const Json& Json::at(int index) const {
    if (m_type == Type::Array && index >= 0 && static_cast<std::size_t>(index) < m_array.size()) {
        return m_array[static_cast<std::size_t>(index)];
    }
    return undefinedValue();
}

const Json& Json::operator[](int index) const {
    return at(index);
}

Json& Json::operator[](int index) {
    if (m_type != Type::Array) {
        *this = array();
    }
    if (index < 0) index = 0;
    if (static_cast<std::size_t>(index) >= m_array.size()) {
        m_array.resize(static_cast<std::size_t>(index) + 1);
    }
    return m_array[static_cast<std::size_t>(index)];
}

void Json::append(const Json& value) {
    if (m_type != Type::Array) {
        *this = array();
    }
    m_array.push_back(value);
}

void Json::push_back(const Json& value) {
    append(value);
}

void Json::insert(int index, const Json& value) {
    if (m_type != Type::Array) {
        *this = array();
    }
    if (index < 0) index = 0;
    if (static_cast<std::size_t>(index) > m_array.size()) {
        index = static_cast<int>(m_array.size());
    }
    m_array.insert(m_array.begin() + index, value);
}

void Json::erase(int index) {
    if (m_type != Type::Array || index < 0 ||
        static_cast<std::size_t>(index) >= m_array.size()) {
        return;
    }
    m_array.erase(m_array.begin() + index);
}

std::string Json::const_iterator::key() const {
    if (m_container && m_container->m_type == Type::Object &&
        m_index < m_container->m_object.size()) {
        return m_container->m_object[m_index].first;
    }
    return std::to_string(m_index);
}

const Json& Json::const_iterator::value() const {
    if (m_container) {
        if (m_container->m_type == Type::Object && m_index < m_container->m_object.size()) {
            return m_container->m_object[m_index].second;
        }
        if (m_container->m_type == Type::Array && m_index < m_container->m_array.size()) {
            return m_container->m_array[m_index];
        }
    }
    return undefinedValue();
}

Json Json::parse(const std::string& text, bool* ok, std::string* error) {
    if (ok) *ok = false;
    if (error) error->clear();

    Json result(UndefinedTag{});
    std::string parseError;
    Parser parser(text);
    if (!parser.run(result, parseError)) {
        if (error) *error = parseError;
        return Json(UndefinedTag{});
    }
    if (ok) *ok = true;
    return result;
}

std::string Json::numberToString(double value) {
    if (!std::isfinite(value)) return "null";
    if (value == std::floor(value) && std::fabs(value) < 9.007199254740992e15) {
        return std::to_string(static_cast<long long>(value));
    }
    char buffer[64];
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
        if (std::strtod(buffer, nullptr) == value) return buffer;
    }
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    return buffer;
}

std::string Json::dump(bool pretty) const {
    std::string out;
    dumpTo(out, pretty, 0);
    if (pretty) out += '\n';
    return out;
}

void Json::dumpTo(std::string& out, bool pretty, int depth) const {
    switch (m_type) {
    case Type::Bool:
        out += m_bool ? "true" : "false";
        break;
    case Type::Number:
        if (m_isInt) {
            out += std::to_string(m_int);
        } else if (std::isfinite(m_double)) {
            out += numberToString(m_double);
        } else {
            out += "null";
        }
        break;
    case Type::String:
        appendEscaped(out, m_string);
        break;
    case Type::Array: {
        if (m_array.empty()) {
            out += "[]";
            break;
        }
        out += '[';
        for (std::size_t i = 0; i < m_array.size(); ++i) {
            if (i > 0) out += ',';
            if (pretty) {
                out += '\n';
                out.append(static_cast<std::size_t>(depth + 1) * 4, ' ');
            }
            m_array[i].dumpTo(out, pretty, depth + 1);
        }
        if (pretty) {
            out += '\n';
            out.append(static_cast<std::size_t>(depth) * 4, ' ');
        }
        out += ']';
        break;
    }
    case Type::Object: {
        if (m_object.empty()) {
            out += "{}";
            break;
        }
        out += '{';
        for (std::size_t i = 0; i < m_object.size(); ++i) {
            if (i > 0) out += ',';
            if (pretty) {
                out += '\n';
                out.append(static_cast<std::size_t>(depth + 1) * 4, ' ');
            }
            appendEscaped(out, m_object[i].first);
            out += pretty ? ": " : ":";
            m_object[i].second.dumpTo(out, pretty, depth + 1);
        }
        if (pretty) {
            out += '\n';
            out.append(static_cast<std::size_t>(depth) * 4, ' ');
        }
        out += '}';
        break;
    }
    case Type::Null:
    case Type::Undefined:
    default:
        out += "null";
        break;
    }
}

bool operator==(const Json& a, const Json& b) {
    if (a.m_type != b.m_type) return false;
    switch (a.m_type) {
    case Json::Type::Undefined:
    case Json::Type::Null: return true;
    case Json::Type::Bool: return a.m_bool == b.m_bool;
    case Json::Type::Number: {
        if (a.m_isInt && b.m_isInt) return a.m_int == b.m_int;
        double left = a.toDouble();
        double right = b.toDouble();
        if (left == right) return true;
        double scale = std::max(1.0, std::max(std::fabs(left), std::fabs(right)));
        return std::fabs(left - right) <= 1e-12 * scale;
    }
    case Json::Type::String: return a.m_string == b.m_string;
    case Json::Type::Array: return a.m_array == b.m_array;
    case Json::Type::Object: {
        if (a.m_object.size() != b.m_object.size()) return false;
        for (const auto& entry : a.m_object) {
            const Json& other = b[entry.first];
            if (other.isUndefined() || !(entry.second == other)) return false;
        }
        return true;
    }
    }
    return false;
}

bool operator!=(const Json& a, const Json& b) {
    return !(a == b);
}

} // namespace ks
