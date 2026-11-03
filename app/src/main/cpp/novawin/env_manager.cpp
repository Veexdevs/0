// NovaWin — gestão de ambientes + preparação do runtime (extração tar.gz)
#include "env_manager.h"

#include "launcher.h"

#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <zlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctime>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <functional>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NovaWin", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "NovaWin", __VA_ARGS__)

namespace nw {

GlobalState& GlobalState::instance() {
    static GlobalState g;
    return g;
}

std::string GlobalState::optString(const std::string& key, const std::string& def) const {
    std::lock_guard<std::mutex> lk(mu);
    auto it = opts.find(key);
    return it == opts.end() ? def : it->second;
}
int GlobalState::optInt(const std::string& key, int def) const {
    std::string v = optString(key, "");
    if (v.empty()) return def;
    return atoi(v.c_str());
}
void GlobalState::setOpt(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lk(mu);
    opts[key] = value;
}

// ---------------- gzip + tar ----------------

namespace {

class GzReader {
public:
    bool init(const unsigned char* data, size_t len) {
        if (inflateInit2(&strm_, 16 + MAX_WBITS) != Z_OK) return false; // gzip
        if (data && len) {
            strm_.next_in = (Bytef*)(void*)data;
            strm_.avail_in = (uInt)len;
        }
        ok_ = true;
        return true;
    }
    // lê até out.size() bytes (menos se fim)
    size_t read(unsigned char* out, size_t n) {
        size_t total = 0;
        while (total < n && ok_) {
            uInt outAvail = (uInt)std::min<size_t>(n - total, 65536);
            strm_.next_out = (Bytef*)(out + total);
            strm_.avail_out = outAvail;
            int rc = inflate(&strm_, Z_NO_FLUSH);
            if (rc == Z_STREAM_END) { ok_ = false; break; }
            if (rc != Z_OK && rc != Z_BUF_ERROR) { ok_ = false; break; }
            total += outAvail - strm_.avail_out;
        }
        return total;
    }
    bool eof() const { return !ok_ && strm_.avail_in == 0; }
    ~GzReader() { inflateEnd(&strm_); }
private:
    z_stream strm_{};
    bool ok_ = false;
};

bool parseOctal(const unsigned char* p, size_t n, long long& out) {
    out = 0;
    bool any = false;
    for (size_t i = 0; i < n; i++) {
        char c = (char)p[i];
        if (c == 0 || c == ' ') continue;
        if (c < '0' || c > '7') return false;
        out = out * 8 + (c - '0');
        any = true;
    }
    return any || n == 0;
}

// extrai um tar.gz completo (memória) para um diretório de saída
bool extractTarGz(const unsigned char* data, size_t len, const std::string& outDir,
                  const std::function<void(long long, long long)>& progress) {
    GzReader gz;
    if (!gz.init(data, len)) return false;

    const size_t hdrSize = 512;
    unsigned char hdr[hdrSize];
    size_t got = gz.read(hdr, hdrSize);
    // primeira passada rápida: total (para progresso) — fazemos incremental:
    // progresso por bytes descomprimidos; total estimado = tamanho do gzip * 2.5
    long long estTotal = (long long)len * 3 / 2;
    long long doneBytes = 0;

    while (got == hdrSize) {
        // fim do arquiv
        bool allZero = true;
        for (size_t i = 0; i < hdrSize; i++) if (hdr[i]) { allZero = false; break; }
        if (allZero) break;

        std::string name(100, 0);
        for (int i = 0; i < 100; i++) name[i] = (char)hdr[i];
        // prefix (ustar): presente quando magic == "ustar"
        std::string prefix;
        {
            char pfx[156];
            memcpy(pfx, hdr + 345, 155);
            pfx[155] = 0;
            if (pfx[0] != 0) prefix = pfx;
        }
        long long size = 0;
        parseOctal(hdr + 124, 12, size);
        char type = (char)hdr[156];
        char link[101];
        memcpy(link, hdr + 157, 100);
        link[100] = 0;
        std::string linkName = link;

        std::string full = name;
        if (!prefix.empty()) full = prefix + "/" + full;
        std::string dest = outDir + "/" + full;

        // proteção contra path traversal
        if (full.find("..") != std::string::npos) {
            // pula este item
            size_t skip = (size + 511) & ~(size_t)511;
            std::vector<unsigned char> skipBuf(512);
            long long skipped = 0;
            while (skipped < (long long)skip) {
                size_t r = gz.read(skipBuf.data(), (size_t)std::min<long long>(512, (long long)skip - skipped));
                if (r == 0) break;
                skipped += r;
            }
            continue;
        }

        if (type == '5') { // diretório
            mkdirs(dest);
        } else if (type == '2') { // symlink
            mkdirs(dirName(dest));
            symlink(linkName.c_str(), dest.c_str());
        } else if (type == '0' || type == '\0' || type == '7') { // arquivo
            mkdirs(dirName(dest));
            FILE* f = fopen(dest.c_str(), "wb");
            if (!f) { LOGE("não abriu %s", dest.c_str()); return false; }
            long long written = 0;
            std::vector<unsigned char> buf(65536);
            while (written < size) {
                size_t r = gz.read(buf.data(),
                                   (size_t)std::min<long long>(65536, size - written));
                if (r == 0) { fclose(f); return false; }
                fwrite(buf.data(), 1, r, f);
                written += r;
            }
            fclose(f);
            // mantém permissão executável (campo mode)
            long long mode = 0;
            parseOctal(hdr + 100, 8, mode);
            if (mode & 0111) chmod(dest.c_str(), 0755);
            doneBytes += size;
            progress(doneBytes, estTotal);
        } else {
            LOGI("tipo tar desconhecido %c (%s)", type, full.c_str());
        }

        // pula padding
        size_t skip = (size + 511) & ~(size_t)511;
        if (skip > 0) {
            std::vector<unsigned char> skipBuf(512);
            long long skipped = 0;
            while (skipped < (long long)skip) {
                size_t r = gz.read(skipBuf.data(), (size_t)std::min<long long>(512, (long long)skip - skipped));
                if (r == 0) break;
                skipped += r;
            }
        }
        got = gz.read(hdr, hdrSize);
    }
    return true;
}

} // namespace

bool Prepare::rootfsReady() {
    auto& g = GlobalState::instance();
    return isDir(g.rootfsDir) &&
           fileExists(g.rootfsDir + "/usr/local/bin/box64") &&
           fileExists(g.rootfsDir + "/usr/local/bin/novawin-session.sh") &&
           fileExists(g.rootfsDir + "/.novawin/ready");
}

int Prepare::run(void* assetManager, const std::function<void(long long, long long)>& progress) {
    auto& g = GlobalState::instance();
    AAssetManager* am = (AAssetManager*)assetManager;
    if (!am) return -1;
    g.prepareState = 1; // em andamento
    try {
        // 1) box64 / box86 (binários ARM64 nativos)
        const char* emus[] = {"box64", "box86"};
        for (auto name : emus) {
            AAsset* a = AAssetManager_open(am, name, AASSET_MODE_STREAMING);
            if (!a) continue;
            long long len = AAsset_getLength(a);
            std::vector<unsigned char> buf(len > 0 ? (size_t)len : 0);
            long long off = 0;
            while (off < len) {
                int r = AAsset_read(a, buf.data() + off, (size_t)(len - off));
                if (r <= 0) break;
                off += r;
            }
            AAsset_close(a);
            std::string dest = g.rootfsDir + "/usr/local/bin/" + name;
            mkdirs(g.rootfsDir + "/usr/local/bin");
            writeWholeFile(dest, std::string((const char*)buf.data(), buf.size()));
            chmod(dest.c_str(), 0755);
        }
        // 2) rootfs.tar.gz
        AAsset* a = AAssetManager_open(am, "rootfs.tar.gz", AASSET_MODE_STREAMING);
        if (!a) {
            g.prepareState = -1;
            return -2; // asset ausente
        }
        long long len = AAsset_getLength(a);
        std::vector<unsigned char> buf((size_t)len);
        long long off = 0;
        while (off < len) {
            int r = AAsset_read(a, buf.data() + off, (size_t)(len - off));
            if (r <= 0) break;
            off += r;
        }
        AAsset_close(a);
        progress(0, 1);
        bool ok = extractTarGz(buf.data(), buf.size(), g.rootfsDir, progress);
        progress(1, 1);
        if (!ok) {
            g.prepareState = -1;
            return -3;
        }
        // 3) limpeza e marca de pronto
        // remove o tar.gz do rootfs (se o pacote o incluía)
        writeWholeFile(g.rootfsDir + "/.novawin/ready", "1");
        g.prepareState = 2;
        return 0;
    } catch (const std::exception& e) {
        LOGE("prepare falhou: %s", e.what());
        g.prepareState = -1;
        return -4;
    }
}

// ---------------- ambientes ----------------

std::string EnvManager::create(const std::string& jsonConfig) {
    auto& g = GlobalState::instance();
    JsonValue cfg = jsonParse(jsonConfig);
    std::string dirName = cfg.getString("dirName");
    if (dirName.empty()) {
        return jsonDump(JsonValue().set("ok", false)
            .set("error", "dirName ausente na configuração do ambiente"), false);
    }
    std::string envDir = joinPath(joinPath(g.rootfsDir, "envs"), dirName);
    if (!isDir(g.rootfsDir)) {
        return jsonDump(JsonValue().set("ok", false)
            .set("error", "runtime não preparado (rootfs ausente)"), false);
    }
    // diretórios do ambiente
    mkdirs(joinPath(envDir, "apps"));
    mkdirs(joinPath(envDir, "tmp"));
    mkdirs(joinPath(envDir, "home"));
    mkdirs(joinPath(envDir, "prefix"));
    // meta
    JsonValue meta;
    meta.set("name", cfg.getString("name", dirName));
    meta.set("arch", cfg.getInt("arch", 0));
    meta.set("resolution", cfg.getString("resolution", "1280x720"));
    meta.set("createdAt", (int)time(nullptr));
    meta.set("wineVersion", g.wineVersion);
    writeWholeFile(joinPath(envDir, "meta.json"), jsonDump(meta, false));
    return jsonDump(JsonValue().set("ok", true), false);
}

int EnvManager::remove(const std::string& envId) {
    auto& g = GlobalState::instance();
    // descobre dirName
    std::string text;
    std::string dirName = envId;
    if (readWholeFile(g.dataDir + "/environments.json", text)) {
        JsonValue arr = jsonParse(text);
        if (arr.type == JsonValue::ARR) {
            for (auto& v : arr.arr)
                if (v.getString("id") == envId)
                    dirName = v.getString("dirName", envId);
        }
    }
    deleteTree(joinPath(joinPath(g.rootfsDir, "envs"), dirName));
    return 0;
}

int EnvManager::initPrefix(const std::string& envId,
                           const std::function<void(const std::string&)>& lineCb) {
    Launcher& L = Launcher::instance();
    GuestEnv env = L.envFor(envId);
    if (!isDir(env.envDir)) return -1;
    env.wineDebug = "warn+all";
    env.dynarec = 1;
    mkdirs(joinPath(env.envDir, "home"));
    const char* wineBin = env.arch == 1 ? "wine32" : "wine64";
    std::vector<std::string> argv;
    argv.push_back(wineBin);
    argv.push_back("wineboot");
    argv.push_back("--init");
    LOGI("wineboot no ambiente %s", envId.c_str());
    return runGuestCaptured(env.boxPath(), argv, env, env.rootfs, lineCb);
}

} // namespace nw
