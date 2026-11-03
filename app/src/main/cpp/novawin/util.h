// NovaWin — utilitários comuns (JSON mínimo + filesystem)
// Núcleo nativo do NovaWin (arm64-android, NDK)
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nw {

// ---------------- JSON (mini-parser/escrita) ----------------

struct JsonValue {
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;

    const JsonValue* find(const std::string& key) const {
        if (type != OBJ) return nullptr;
        for (auto& kv : obj)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    std::string getString(const std::string& key, const std::string& def = "") const {
        const JsonValue* v = find(key);
        return (v && v->type == STR) ? v->str : def;
    }
    int getInt(const std::string& key, int def = 0) const {
        const JsonValue* v = find(key);
        return v ? (int)v->num : def;
    }
    bool getBool(const std::string& key, bool def = false) const {
        const JsonValue* v = find(key);
        return (v && v->type == BOOL) ? v->b : def;
    }
    JsonValue& set(const std::string& key, const std::string& val) {
        if (type != OBJ) type = OBJ;
        for (auto& kv : obj)
            if (kv.first == key) { kv.second = JsonValue(); kv.second.str = val; kv.second.type = STR; return kv.second; }
        obj.emplace_back(key, JsonValue());
        obj.back().second.str = val;
        obj.back().second.type = STR;
        return obj.back().second;
    }
    JsonValue& set(const std::string& key, int val) {
        if (type != OBJ) type = OBJ;
        for (auto& kv : obj)
            if (kv.first == key) { kv.second = JsonValue(); kv.second.num = val; kv.second.type = NUM; return kv.second; }
        obj.emplace_back(key, JsonValue());
        obj.back().second.num = val;
        obj.back().second.type = NUM;
        return obj.back().second;
    }
    JsonValue& set(const std::string& key, bool val) {
        if (type != OBJ) type = OBJ;
        for (auto& kv : obj)
            if (kv.first == key) { kv.second = JsonValue(); kv.second.b = val; kv.second.type = BOOL; return kv.second; }
        obj.emplace_back(key, JsonValue());
        obj.back().second.b = val;
        obj.back().second.type = BOOL;
        return obj.back().second;
    }
};

// Retorna NUL em caso de erro de parse.
JsonValue jsonParse(const std::string& text);
std::string jsonDump(const JsonValue& v, bool pretty = false);
std::string jsonEscape(const std::string& s);

// ---------------- Filesystem ----------------

bool fileExists(const std::string& path);
bool isDir(const std::string& path);
long long fileSize(const std::string& path);
bool readWholeFile(const std::string& path, std::string& out);
bool writeWholeFile(const std::string& path, const std::string& data);
bool mkdirs(const std::string& path);
bool deleteTree(const std::string& path);
bool copyTree(const std::string& src, const std::string& dst);
bool moveTree(const std::string& src, const std::string& dst);
std::string joinPath(const std::string& a, const std::string& b);
std::string baseName(const std::string& path);
std::string dirName(const std::string& path);

struct DirEntry {
    std::string name;
    std::string path;
    bool isDirectory = false;
    long long size = 0;
    long long mtime = 0;
};
bool listDir(const std::string& path, std::vector<DirEntry>& out);

} // namespace nw
