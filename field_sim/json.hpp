#pragma once
// Minimal strict JSON value, parser and writer for the field_sim line protocol.
// Objects keep insertion order; duplicate keys are rejected. Numbers are doubles.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace field_sim::json {
struct Error : std::runtime_error { using std::runtime_error::runtime_error; };

class Value {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<Value>;
    using Object = std::vector<std::pair<std::string, Value>>;
    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : kind_(Kind::Bool), bool_(b) {}
    Value(double n) : kind_(Kind::Number), num_(n) {}
    Value(int n) : kind_(Kind::Number), num_(n) {}
    Value(long long n) : kind_(Kind::Number), num_(double(n)) {}
    Value(std::size_t n) : kind_(Kind::Number), num_(double(n)) {}
    Value(const char* s) : kind_(Kind::String), str_(s) {}
    Value(std::string s) : kind_(Kind::String), str_(std::move(s)) {}
    Value(Array a) : kind_(Kind::Array), arr_(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : kind_(Kind::Object), obj_(std::make_shared<Object>(std::move(o))) {}
    static Value object() { return Value(Object{}); }
    static Value array() { return Value(Array{}); }

    Kind kind() const { return kind_; }
    bool is_null() const { return kind_ == Kind::Null; }
    bool is_bool() const { return kind_ == Kind::Bool; }
    bool is_number() const { return kind_ == Kind::Number; }
    bool is_string() const { return kind_ == Kind::String; }
    bool is_array() const { return kind_ == Kind::Array; }
    bool is_object() const { return kind_ == Kind::Object; }
    bool as_bool() const { need(Kind::Bool, "boolean"); return bool_; }
    double as_number() const { need(Kind::Number, "number"); return num_; }
    const std::string& as_string() const { need(Kind::String, "string"); return str_; }
    const Array& as_array() const { need(Kind::Array, "array"); return *arr_; }
    const Object& as_object() const { need(Kind::Object, "object"); return *obj_; }
    long long as_int() const {
        double v = as_number();
        if (v != std::floor(v) || std::fabs(v) > 9.0e15) throw Error("expected an integer");
        return static_cast<long long>(v);
    }
    // Object access. find() returns nullptr when absent.
    const Value* find(const std::string& key) const {
        if (kind_ != Kind::Object) return nullptr;
        for (const auto& kv : *obj_) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    const Value& at(const std::string& key) const {
        const Value* v = find(key);
        if (!v) throw Error("missing key '" + key + "'");
        return *v;
    }
    double number_or(const std::string& key, double fallback) const { const Value* v = find(key); return v ? v->as_number() : fallback; }
    bool bool_or(const std::string& key, bool fallback) const { const Value* v = find(key); return v ? v->as_bool() : fallback; }
    std::string string_or(const std::string& key, const std::string& fallback) const { const Value* v = find(key); return v ? v->as_string() : fallback; }
    Value& set(const std::string& key, Value v) {
        need(Kind::Object, "object");
        for (auto& kv : *obj_) if (kv.first == key) { kv.second = std::move(v); return kv.second; }
        obj_->emplace_back(key, std::move(v));
        return obj_->back().second;
    }
    void push(Value v) { need(Kind::Array, "array"); arr_->push_back(std::move(v)); }

    std::string dump() const { std::string out; write(out); return out; }
    void write(std::string& out) const {
        switch (kind_) {
        case Kind::Null: out += "null"; break;
        case Kind::Bool: out += bool_ ? "true" : "false"; break;
        case Kind::Number: {
            if (!std::isfinite(num_)) { out += "null"; break; }
            char buf[40];
            if (num_ == std::floor(num_) && std::fabs(num_) < 1.0e15) std::snprintf(buf, sizeof buf, "%.0f", num_);
            else std::snprintf(buf, sizeof buf, "%.9g", num_);
            out += buf;
            break;
        }
        case Kind::String: quote(str_, out); break;
        case Kind::Array: {
            out += '[';
            bool first = true;
            for (const auto& v : *arr_) { if (!first) out += ','; first = false; v.write(out); }
            out += ']';
            break;
        }
        case Kind::Object: {
            out += '{';
            bool first = true;
            for (const auto& kv : *obj_) { if (!first) out += ','; first = false; quote(kv.first, out); out += ':'; kv.second.write(out); }
            out += '}';
            break;
        }
        }
    }
    static void quote(const std::string& s, std::string& out) {
        out += '"';
        for (unsigned char c : s) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
                else out += static_cast<char>(c);
            }
        }
        out += '"';
    }

private:
    Kind kind_ = Kind::Null;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    std::shared_ptr<Array> arr_;
    std::shared_ptr<Object> obj_;
    void need(Kind k, const char* what) const { if (kind_ != k) throw Error(std::string("expected ") + what); }
};

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}
    Value parse() {
        Value v = value(0);
        space();
        if (i_ != s_.size()) fail("trailing characters");
        return v;
    }
private:
    const std::string& s_;
    std::size_t i_ = 0;
    [[noreturn]] void fail(const std::string& what) const { throw Error("JSON: " + what + " at offset " + std::to_string(i_)); }
    void space() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_; }
    bool eat(const char* word) {
        std::size_t n = std::char_traits<char>::length(word);
        if (s_.compare(i_, n, word) == 0) { i_ += n; return true; }
        return false;
    }
    Value value(int depth) {
        if (depth > 64) fail("nesting too deep");
        space();
        if (i_ >= s_.size()) fail("unexpected end");
        char c = s_[i_];
        if (c == '{') {
            ++i_;
            Value::Object o;
            space();
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; return Value(std::move(o)); }
            for (;;) {
                space();
                if (i_ >= s_.size() || s_[i_] != '"') fail("expected a key");
                std::string key = string();
                for (const auto& kv : o) if (kv.first == key) fail("duplicate key '" + key + "'");
                space();
                if (i_ >= s_.size() || s_[i_] != ':') fail("expected ':'");
                ++i_;
                o.emplace_back(std::move(key), value(depth + 1));
                space();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == '}') { ++i_; break; }
                fail("expected ',' or '}'");
            }
            return Value(std::move(o));
        }
        if (c == '[') {
            ++i_;
            Value::Array a;
            space();
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; return Value(std::move(a)); }
            for (;;) {
                a.push_back(value(depth + 1));
                space();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == ']') { ++i_; break; }
                fail("expected ',' or ']'");
            }
            return Value(std::move(a));
        }
        if (c == '"') return Value(string());
        if (eat("true")) return Value(true);
        if (eat("false")) return Value(false);
        if (eat("null")) return Value(nullptr);
        return Value(number());
    }
    double number() {
        std::size_t start = i_;
        if (i_ < s_.size() && s_[i_] == '-') ++i_;
        if (i_ >= s_.size() || s_[i_] < '0' || s_[i_] > '9') fail("invalid value");
        if (s_[i_] == '0') ++i_;
        else while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
        if (i_ < s_.size() && s_[i_] == '.') {
            ++i_;
            if (i_ >= s_.size() || s_[i_] < '0' || s_[i_] > '9') fail("invalid number");
            while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
        }
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            ++i_;
            if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
            if (i_ >= s_.size() || s_[i_] < '0' || s_[i_] > '9') fail("invalid number");
            while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
        }
        double v = 0;
        try { v = std::stod(s_.substr(start, i_ - start)); } catch (...) { fail("number out of range"); }
        if (!std::isfinite(v)) fail("number out of range");
        return v;
    }
    std::string string() {
        ++i_;
        std::string out;
        for (;;) {
            if (i_ >= s_.size()) fail("unterminated string");
            unsigned char c = static_cast<unsigned char>(s_[i_++]);
            if (c == '"') break;
            if (c < 0x20) fail("control character in string");
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (i_ >= s_.size()) fail("unterminated escape");
            char e = s_[i_++];
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
                unsigned cp = hex4();
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (i_ + 1 >= s_.size() || s_[i_] != '\\' || s_[i_ + 1] != 'u') fail("lone surrogate");
                    i_ += 2;
                    unsigned lo = hex4();
                    if (lo < 0xDC00 || lo > 0xDFFF) fail("invalid surrogate pair");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) fail("lone surrogate");
                utf8(cp, out);
                break;
            }
            default: fail("invalid escape");
            }
        }
        return out;
    }
    unsigned hex4() {
        if (i_ + 4 > s_.size()) fail("short \\u escape");
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else fail("invalid \\u escape");
        }
        return v;
    }
    static void utf8(unsigned cp, std::string& out) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
};
inline Value parse(const std::string& text) { return Parser(text).parse(); }
} // namespace field_sim::json
