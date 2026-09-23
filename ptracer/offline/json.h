// Minimal JSON reader/writer for the PTracer offline tools (spec, site map, sideband, summary).
// Supports objects, arrays, strings, numbers (int64/double), bool, null. No dependencies.
#pragma once
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <cerrno>
#include <cstring>
#include <vector>

struct Json {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
    bool b = false; double num = 0; int64_t inum = 0; bool is_int = false; std::string s;
    std::vector<Json> arr; std::map<std::string, Json> obj;
    const Json& operator[](const std::string& k) const { static Json none; auto it = obj.find(k); return it == obj.end() ? none : it->second; }
    const Json& operator[](size_t i) const { static Json none; return i < arr.size() ? arr[i] : none; }
    bool has(const std::string& k) const { return obj.count(k) != 0; }
    int64_t i64(int64_t d = 0) const { return t == NUM ? (is_int ? inum : (int64_t)num) : d; }
    uint64_t u64(uint64_t d = 0) const { return t == NUM ? (is_int ? (uint64_t)inum : (uint64_t)num) : d; }
    double dbl(double d = 0) const { return t == NUM ? (is_int ? (double)inum : num) : d; }
    const std::string& str() const { return s; }
    bool boolean(bool d = false) const { return t == BOOL ? b : d; }
    size_t size() const { return t == ARR ? arr.size() : t == OBJ ? obj.size() : 0; }
    bool isnull() const { return t == NUL; }
};

struct JsonParser {
    const char* p; const char* e;
    JsonParser(const std::string& s) : p(s.data()), e(s.data() + s.size()) {}
    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')) p++; }
    [[noreturn]] void fail(const char* m) { throw std::runtime_error(std::string("json: ") + m); }
    Json parse() {
        ws(); if (p >= e) fail("eof");
        Json j;
        if (*p == '{') { j.t = Json::OBJ; p++; ws(); if (*p == '}') { p++; return j; }
            for (;;) { ws(); if (*p != '"') fail("key"); std::string k = pstr(); ws(); if (*p != ':') fail(":"); p++;
                j.obj[k] = parse(); ws(); if (*p == ',') { p++; continue; } if (*p == '}') { p++; return j; } fail("obj"); } }
        if (*p == '[') { j.t = Json::ARR; p++; ws(); if (*p == ']') { p++; return j; }
            for (;;) { j.arr.push_back(parse()); ws(); if (*p == ',') { p++; continue; } if (*p == ']') { p++; return j; } fail("arr"); } }
        if (*p == '"') { j.t = Json::STR; j.s = pstr(); return j; }
        if (!strncmp(p, "true", 4)) { p += 4; j.t = Json::BOOL; j.b = true; return j; }
        if (!strncmp(p, "false", 5)) { p += 5; j.t = Json::BOOL; j.b = false; return j; }
        if (!strncmp(p, "null", 4)) { p += 4; return j; }
        // number
        const char* s = p; bool isint = true;
        if (*p == '-') p++;
        while (p < e && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) { if (*p == '.' || *p == 'e' || *p == 'E') isint = false; p++; }
        std::string n(s, p); if (n.empty()) fail("value");
        j.t = Json::NUM; j.is_int = isint;
        if (isint) { j.inum = strtoll(n.c_str(), nullptr, 10); j.num = (double)j.inum; }
        else { j.num = strtod(n.c_str(), nullptr); j.inum = (int64_t)j.num; }
        return j;
    }
    std::string pstr() {
        p++; std::string r;
        while (p < e && *p != '"') {
            if (*p == '\\') { p++; char c = *p++; switch (c) { case 'n': r += '\n'; break; case 't': r += '\t'; break; case 'r': r += '\r'; break;
                case 'u': { unsigned v = strtoul(std::string(p, p + 4).c_str(), nullptr, 16); p += 4; if (v < 0x80) r += (char)v; else if (v < 0x800) { r += (char)(0xC0 | (v >> 6)); r += (char)(0x80 | (v & 0x3F)); } else { r += (char)(0xE0 | (v >> 12)); r += (char)(0x80 | ((v >> 6) & 0x3F)); r += (char)(0x80 | (v & 0x3F)); } break; }
                default: r += c; } }
            else r += *p++;
        }
        if (p >= e) fail("string");
        p++;
        return r;
    }
};
inline Json json_parse(const std::string& s) { JsonParser jp(s); return jp.parse(); }
inline Json json_load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb"); if (!f) throw std::runtime_error("cannot open " + path);
    // A SHORT read must never be mistaken for end-of-file: a truncated buffer parses as a
    // syntax error at an arbitrary offset ("json: :").  Read until feof, and retry an
    // interrupted read instead of stopping at it.
    std::string s; char buf[65536]; size_t n;
    for (;;) {
        n = fread(buf, 1, sizeof buf, f);
        if (n) { s.append(buf, n); continue; }
        if (feof(f)) break;
        if (ferror(f) && errno == EINTR) { clearerr(f); continue; }
        int e = errno; fclose(f);
        throw std::runtime_error("short read on " + path + ": " + strerror(e));
    }
    fclose(f);
    // Name the file in the exception: a bare "json: :" says nothing about WHICH of the
    // JSON inputs failed to parse.
    try { return json_parse(s); }
    catch (const std::exception& e) {
        throw std::runtime_error(std::string(e.what()) + " while parsing " + path +
                                 " (" + std::to_string(s.size()) + " bytes read)");
    }
}
inline std::string json_quote(const std::string& s) { std::string r = "\""; for (char c : s) { if (c == '"' || c == '\\') { r += '\\'; r += c; } else if (c == '\n') r += "\\n"; else r += c; } return r + "\""; }

// Shared by the threaded and chunked summary writers.
inline std::string json_dump(const Json& j) {
    switch (j.t) {
        case Json::NUL: return "null";
        case Json::BOOL: return j.b ? "true" : "false";
        case Json::NUM: if (j.is_int) return std::to_string(j.inum); else { char b[64]; snprintf(b, sizeof b, "%.17g", j.num); return b; }
        case Json::STR: return json_quote(j.s);
        case Json::ARR: { std::string s = "["; bool first = true; for (const auto& x : j.arr) { if (!first) s += ","; s += json_dump(x); first = false; } return s + "]"; }
        case Json::OBJ: { std::string s = "{"; bool first = true; for (const auto& kv : j.obj) { if (!first) s += ","; s += json_quote(kv.first) + ":" + json_dump(kv.second); first = false; } return s + "}"; }
    }
    return "null";
}
