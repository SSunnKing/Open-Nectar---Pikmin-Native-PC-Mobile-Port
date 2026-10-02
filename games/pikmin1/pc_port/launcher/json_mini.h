#pragma once

// JSON mínimo para el launcher: lee las respuestas del juego (ajustes) y de
// la API de GitHub (último release). Sin dependencias.
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace pikmin {
namespace launcher {

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Json> items;
    std::map<std::string, Json> fields;

    const Json* get(const std::string& key) const
    {
        const auto it = fields.find(key);
        return it == fields.end() ? nullptr : &it->second;
    }
    std::string str(const std::string& key) const
    {
        const Json* v = get(key);
        return v && v->type == Type::String ? v->text : std::string();
    }
    int integer(const std::string& key, int fallback) const
    {
        const Json* v = get(key);
        return v && v->type == Type::Number ? int(v->number) : fallback;
    }
    bool flag(const std::string& key, bool fallback) const
    {
        const Json* v = get(key);
        return v && v->type == Type::Bool ? v->boolean : fallback;
    }
};

struct Parser {
    const std::string& s;
    std::size_t at = 0;

    void skip()
    {
        while (at < s.size() && (s[at] == ' ' || s[at] == '\t' || s[at] == '\n' || s[at] == '\r')) ++at;
    }
    bool literal(const char* word)
    {
        std::size_t n = 0;
        while (word[n]) ++n;
        if (s.compare(at, n, word) != 0) return false;
        at += n;
        return true;
    }
    static void appendUtf8(std::string& out, unsigned code)
    {
        if (code < 0x80) out += char(code);
        else if (code < 0x800) { out += char(0xC0 | (code >> 6)); out += char(0x80 | (code & 0x3F)); }
        else { out += char(0xE0 | (code >> 12)); out += char(0x80 | ((code >> 6) & 0x3F)); out += char(0x80 | (code & 0x3F)); }
    }
    bool string(std::string& out)
    {
        if (at >= s.size() || s[at] != '"') return false;
        ++at;
        while (at < s.size() && s[at] != '"') {
            char c = s[at++];
            if (c != '\\') { out += c; continue; }
            if (at >= s.size()) return false;
            c = s[at++];
            switch (c) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u':
                if (at + 4 > s.size()) return false;
                appendUtf8(out, unsigned(std::strtoul(s.substr(at, 4).c_str(), nullptr, 16)));
                at += 4;
                break;
            default: out += c; break; // \" \\ \/
            }
        }
        if (at >= s.size()) return false;
        ++at;
        return true;
    }
    bool value(Json& out, int depth = 0)
    {
        if (depth > 32) return false;
        skip();
        if (at >= s.size()) return false;
        const char c = s[at];
        if (c == '{') {
            out.type = Json::Type::Object;
            ++at;
            skip();
            if (at < s.size() && s[at] == '}') { ++at; return true; }
            for (;;) {
                skip();
                std::string key;
                if (!string(key)) return false;
                skip();
                if (at >= s.size() || s[at] != ':') return false;
                ++at;
                if (!value(out.fields[key], depth + 1)) return false;
                skip();
                if (at < s.size() && s[at] == ',') { ++at; continue; }
                if (at < s.size() && s[at] == '}') { ++at; return true; }
                return false;
            }
        }
        if (c == '[') {
            out.type = Json::Type::Array;
            ++at;
            skip();
            if (at < s.size() && s[at] == ']') { ++at; return true; }
            for (;;) {
                out.items.emplace_back();
                if (!value(out.items.back(), depth + 1)) return false;
                skip();
                if (at < s.size() && s[at] == ',') { ++at; continue; }
                if (at < s.size() && s[at] == ']') { ++at; return true; }
                return false;
            }
        }
        if (c == '"') {
            out.type = Json::Type::String;
            return string(out.text);
        }
        if (literal("true")) { out.type = Json::Type::Bool; out.boolean = true; return true; }
        if (literal("false")) { out.type = Json::Type::Bool; out.boolean = false; return true; }
        if (literal("null")) { out.type = Json::Type::Null; return true; }
        char* end = nullptr;
        out.number = std::strtod(s.c_str() + at, &end);
        if (end == s.c_str() + at) return false;
        out.type = Json::Type::Number;
        at = std::size_t(end - s.c_str());
        return true;
    }
};


// Analiza `text` entero. false si no es JSON válido.
inline bool parseJson(const std::string& text, Json& out)
{
    Parser parser { text };
    return parser.value(out);
}

} // namespace launcher
} // namespace pikmin
