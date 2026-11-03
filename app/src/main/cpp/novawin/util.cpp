// NovaWin — utilitários: mini JSON + filesystem
#include "util.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ftw.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <fstream>

namespace nw {

// ---------------- JSON ----------------
// Parser recursivo simples que cobre o subconjunto JSON usado pelo NovaWin.

namespace {
struct Parser {
    const char* p;
    const char* end;
    bool ok = true;

    void skipWs() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    }
    bool consume(char c) {
        skipWs();
        if (p < end && *p == c) { p++; return true; }
        ok = false;
        return false;
    }
    bool parseString(std::string& out) {
        skipWs();
        if (p >= end || *p != '"') { ok = false; return false; }
        p++;
        while (p < end && *p != '"') {
            char c = *p++;
            if (c == '\\' && p < end) {
                char e = *p++;
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'u': {
                        if (p + 4 > end) { ok = false; return false; }
                        unsigned int cp = 0;
                        for (int i = 0; i < 4; i++) {
                            char h = *p++;
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                            else { ok = false; return false; }
                        }
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) {
                            out += (char)(0xC0 | (cp >> 6));
                            out += (char)(0x80 | (cp & 0x3F));
                        } else {
                            out += (char)(0xE0 | (cp >> 12));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: out += e; break;
                }
            } else {
                out += c;
            }
        }
        if (p >= end) { ok = false; return false; }
        p++; // "
        return true;
    }
    JsonValue parseValue() {
        JsonValue v;
        skipWs();
        if (p >= end) { ok = false; return v; }
        char c = *p;
        if (c == '{') {
            p++;
            v.type = JsonValue::OBJ;
            skipWs();
            if (p < end && *p == '}') { p++; return v; }
            while (p < end) {
                std::string key;
                if (!parseString(key)) { v = JsonValue(); return v; }
                if (!consume(':')) { v = JsonValue(); return v; }
                Parser sub{p, end, true};
                JsonValue child = sub.parseValue();
                if (!sub.ok) { v = JsonValue(); return v; }
                p = sub.p;
                v.obj.emplace_back(key, std::move(child));
                skipWs();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == '}') { p++; return v; }
                ok = false;
                v = JsonValue();
                return v;
            }
            ok = false;
            v = JsonValue();
            return v;
        }
        if (c == '[') {
            p++;
            v.type = JsonValue::ARR;
            skipWs();
            if (p < end && *p == ']') { p++; return v; }
            while (p < end) {
                Parser sub{p, end, true};
                JsonValue child = sub.parseValue();
                if (!sub.ok) { v = JsonValue(); return v; }
                p = sub.p;
                v.arr.push_back(std::move(child));
                skipWs();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == ']') { p++; return v; }
                ok = false;
                v = JsonValue();
                return v;
            }
            ok = false;
            v = JsonValue();
            return v;
        }
        if (c == '"') {
            v.type = JsonValue::STR;
            if (!parseString(v.str)) v = JsonValue();
            return v;
        }
        if (c == 't' || c == 'f') {
            if (end - p >= 4 && !strncmp(p, "true", 4)) { v.type = JsonValue::BOOL; v.b = true; p += 4; return v; }
            if (end - p >= 5 && !strncmp(p, "false", 5)) { v.type = JsonValue::BOOL; v.b = false; p += 5; return v; }
            ok = false;
            return v;
        }
        if (c == 'n') {
            if (end - p >= 4 && !strncmp(p, "null", 4)) { p += 4; return v; }
            ok = false;
            return v;
        }
        v.type = JsonValue::NUM;
        char* stop = nullptr;
        v.num = strtod(p, &stop);
        if (stop == p) { ok = false; v = JsonValue(); return v; }
        p = stop;
        return v;
    }
};
} // namespace

JsonValue jsonParse(const std::string& text) {
    Parser pr{text.c_str(), text.c_str() + text.size(), true};
    JsonValue v = pr.parseValue();
    if (!pr.ok) return JsonValue();
    return v;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

static void dumpValue(const JsonValue& v, std::string& out) {
    switch (v.type) {
        case JsonValue::NUL: out += "null"; break;
        case JsonValue::BOOL: out += v.b ? "true" : "false"; break;
        case JsonValue::NUM: {
            if (v.num == (long long)v.num) out += std::to_string((long long)v.num);
            else out += std::to_string(v.num);
            break;
        }
        case JsonValue::STR: out += "\""; out += jsonEscape(v.str); out += "\""; break;
        case JsonValue::ARR: {
            out += "[";
            for (size_t i = 0; i < v.arr.size(); i++) {
                if (i) out += ",";
                dumpValue(v.arr[i], out);
            }
            out += "]";
            break;
        }
        case JsonValue::OBJ: {
            out += "{";
            for (size_t i = 0; i < v.obj.size(); i++) {
                if (i) out += ",";
                out += "\"";
                out += jsonEscape(v.obj[i].first);
                out += "\":";
                dumpValue(v.obj[i].second, out);
            }
            out += "}";
            break;
        }
    }
}

std::string jsonDump(const JsonValue& v, bool pretty) {
    std::string out;
    dumpValue(v, out);
    (void)pretty;
    return out;
}

// ---------------- Filesystem ----------------

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool isDir(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

long long fileSize(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return -1;
    return (long long)st.st_size;
}

bool readWholeFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool writeWholeFile(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), (std::streamsize)data.size());
    return (bool)f;
}

bool mkdirs(const std::string& path) {
    if (path.empty()) return false;
    if (fileExists(path)) return true;
    std::string cur;
    for (size_t i = 0; i < path.size(); i++) {
        cur += path[i];
        if (path[i] == '/' && cur.size() > 1) mkdir(cur.c_str(), 0775);
    }
    mkdir(cur.c_str(), 0775);
    return isDir(path);
}

static bool rmRecursive(const std::string& path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return false;
    if (S_ISDIR(st.st_mode)) {
        DIR* d = opendir(path.c_str());
        if (!d) return false;
        bool ok = true;
        struct dirent* ent;
        std::vector<std::string> children;
        while ((ent = readdir(d)) != nullptr) {
            std::string name = ent->d_name;
            if (name == "." || name == "..") continue;
            children.push_back(joinPath(path, name));
        }
        closedir(d);
        for (auto& c : children) ok = rmRecursive(c) && ok;
        return ok && rmdir(path.c_str()) == 0;
    }
    return remove(path.c_str()) == 0;
}

bool deleteTree(const std::string& path) {
    if (!fileExists(path)) return true;
    return rmRecursive(path);
}

static bool copyRecursive(const std::string& src, const std::string& dst) {
    struct stat st;
    if (lstat(src.c_str(), &st) != 0) return false;
    if (S_ISDIR(st.st_mode)) {
        if (mkdir(dst.c_str(), 0775) != 0 && !isDir(dst)) return false;
        DIR* d = opendir(src.c_str());
        if (!d) return false;
        struct dirent* ent;
        bool ok = true;
        while ((ent = readdir(d)) != nullptr) {
            std::string name = ent->d_name;
            if (name == "." || name == "..") continue;
            ok = copyRecursive(joinPath(src, name), joinPath(dst, name)) && ok;
        }
        closedir(d);
        return ok;
    }
    if (S_ISLNK(st.st_mode)) {
        char buf[4096];
        ssize_t n = readlink(src.c_str(), buf, sizeof(buf) - 1);
        if (n < 0) return false;
        buf[n] = 0;
        unlink(dst.c_str());
        return symlink(buf, dst.c_str()) == 0;
    }
    if (S_ISREG(st.st_mode)) {
        std::ifstream fi(src, std::ios::binary);
        if (!fi) return false;
        std::ofstream fo(dst, std::ios::binary | std::ios::trunc);
        if (!fo) return false;
        fo << fi.rdbuf();
        return (bool)fo;
    }
    return false;
}

bool copyTree(const std::string& src, const std::string& dst) {
    if (!fileExists(src)) return false;
    if (isDir(src)) return copyRecursive(src, dst);
    mkdirs(dirName(dst));
    return copyRecursive(src, dst);
}

bool moveTree(const std::string& src, const std::string& dst) {
    if (!fileExists(src)) return false;
    // tenta rename atomico (mesmo sistema de arquivos)
    if (rename(src.c_str(), dst.c_str()) == 0) return true;
    // fallback: copia + remove (caminho de seguranca)
    if (copyTree(src, dst)) return deleteTree(src);
    return false;
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

std::string baseName(const std::string& path) {
    size_t i = path.find_last_of('/');
    return i == std::string::npos ? path : path.substr(i + 1);
}

std::string dirName(const std::string& path) {
    size_t i = path.find_last_of('/');
    if (i == std::string::npos) return "";
    if (i == 0) return "/";
    return path.substr(0, i);
}

bool listDir(const std::string& path, std::vector<DirEntry>& out) {
    DIR* d = opendir(path.c_str());
    if (!d) return false;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        DirEntry e;
        e.name = name;
        e.path = joinPath(path, name);
        struct stat st;
        if (lstat(e.path.c_str(), &st) == 0) {
            e.isDirectory = S_ISDIR(st.st_mode);
            e.size = (long long)st.st_size;
            e.mtime = (long long)st.st_mtime;
        }
        out.push_back(std::move(e));
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
        return a.name < b.name;
    });
    return true;
}

} // namespace nw
