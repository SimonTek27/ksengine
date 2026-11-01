#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ks {

/**
 * @brief Qt-free JSON value with a QJsonValue/QJsonObject/QJsonArray-like API.
 */
class Json {
public:
    enum class Type {
        Undefined,
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    class const_iterator {
    public:
        const_iterator(const Json* container, std::size_t index)
            : m_container(container), m_index(index) {}

        const Json& operator*() const { return value(); }
        const Json* operator->() const { return &value(); }

        const_iterator& operator++() {
            ++m_index;
            return *this;
        }

        bool operator==(const const_iterator& other) const {
            return m_container == other.m_container && m_index == other.m_index;
        }

        bool operator!=(const const_iterator& other) const { return !(*this == other); }

        std::string key() const;
        const Json& value() const;

    private:
        const Json* m_container;
        std::size_t m_index;
    };

    Json();
    Json(bool value);
    Json(int value);
    Json(long value);       // distinct from long long only where int64_t is long
    Json(long long value);
    Json(double value);
    Json(const char* value);
    Json(const std::string& value);

    static Json object();
    static Json array();

    Type type() const { return m_type; }
    bool isUndefined() const { return m_type == Type::Undefined; }
    bool isNull() const { return m_type == Type::Null; }
    bool isBool() const { return m_type == Type::Bool; }
    bool isDouble() const { return m_type == Type::Number; }
    bool isString() const { return m_type == Type::String; }
    bool isArray() const { return m_type == Type::Array; }
    bool isObject() const { return m_type == Type::Object; }
    bool isInt() const { return m_type == Type::Number && m_isInt; }
    bool isEmpty() const;

    bool toBool(bool defaultValue = false) const;
    int toInt(int defaultValue = 0) const;
    long long toInt64(long long defaultValue = 0) const;
    double toDouble(double defaultValue = 0.0) const;
    std::string toString(const std::string& defaultValue = std::string()) const;

    bool contains(const std::string& key) const;
    Json value(const std::string& key) const;
    const Json& operator[](const std::string& key) const;
    Json& operator[](const std::string& key);
    void insert(const std::string& key, const Json& value);
    void remove(const std::string& key);
    std::vector<std::string> keys() const;

    int size() const;
    const Json& at(int index) const;
    const Json& operator[](int index) const;
    Json& operator[](int index);
    void append(const Json& value);
    void push_back(const Json& value);
    void insert(int index, const Json& value);
    void erase(int index);

    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { return const_iterator(this, size()); }

    static Json parse(const std::string& text, bool* ok = nullptr, std::string* error = nullptr);
    std::string dump(bool pretty = false) const;
    static std::string numberToString(double value);

    friend bool operator==(const Json& a, const Json& b);
    friend bool operator!=(const Json& a, const Json& b);

private:
    struct UndefinedTag {};

    explicit Json(UndefinedTag);

    static const Json& undefinedValue();
    void dumpTo(std::string& out, bool pretty, int depth) const;

    Type m_type = Type::Null;
    bool m_bool = false;
    bool m_isInt = false;
    long long m_int = 0;
    double m_double = 0.0;
    std::string m_string;
    std::vector<Json> m_array;
    std::vector<std::pair<std::string, Json>> m_object;
};

bool operator==(const Json& a, const Json& b);
bool operator!=(const Json& a, const Json& b);

} // namespace ks
