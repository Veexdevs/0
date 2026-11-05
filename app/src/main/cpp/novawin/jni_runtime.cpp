// NovaWin — ponte JNI (Kotlin <-> núcleo nativo)
#include <jni.h>

#include "env_manager.h"
#include "launcher.h"
#include "procmgr.h"
#include "util.h"
#include "xbridge.h"

#include <android/log.h>
#include <SDL.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "NovaWin", __VA_ARGS__)

using namespace nw;

static JavaVM* g_vm = nullptr;

static JNIEnv* env() {
    JNIEnv* e = nullptr;
    if (g_vm->GetEnv((void**)&e, JNI_VERSION_1_6) == JNI_OK) return e;
    g_vm->AttachCurrentThread(&e, nullptr);
    return e;
}

// executa um binário ARM nativo (ex.: box64 --version) e captura a primeira linha
static std::string runNativeCapture(const std::string& bin, const std::vector<std::string>& args) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return "";
    std::vector<std::string> full;
    full.push_back(bin);
    for (auto& a : args) full.push_back(a);
    std::vector<char*> argv;
    for (auto& s : full) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);
    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return ""; }
    if (pid == 0) {
        close(pipefd[0]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, 0);
        dup2(pipefd[1], 1);
        dup2(pipefd[1], 2);
        execv(bin.c_str(), argv.data());
        _exit(127);
    }
    close(pipefd[1]);
    std::string out;
    char buf[4096];
    for (;;) {
        ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n <= 0) break;
        out.append(buf, (size_t)n);
        if (out.size() > 65536) break;
    }
    close(pipefd[0]);
    waitpid(pid, nullptr, 0);
    // primeira linha
    size_t nl = out.find('\n');
    if (nl != std::string::npos) out = out.substr(0, nl);
    return out;
}

extern "C" {

JNIEXPORT void JNICALL
Java_com_novawin_core_Runtime_nativeInit(JNIEnv* jni, jclass, jstring filesDirJ) {
    auto& g = GlobalState::instance();
    const char* p = jni->GetStringUTFChars(filesDirJ, nullptr);
    g.filesDir = p;
    jni->ReleaseStringUTFChars(filesDirJ, p);
    g.rootfsDir = joinPath(g.filesDir, "rootfs");
    g.dataDir = joinPath(g.filesDir, "data");
    mkdirs(g.dataDir);
    g.cores = procCpuCount();
    // versão do wine (marcador gravado na CI no rootfs)
    std::string v;
    if (readWholeFile(g.rootfsDir + "/.novawin/wine_version", v)) {
        size_t nl = v.find('\n');
        if (nl != std::string::npos) v = v.substr(0, nl);
        g.wineVersion = v;
    }
    g.wine32Available = fileExists(g.rootfsDir + "/.novawin/wine32");
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativePrepareState(JNIEnv*, jclass) {
    return GlobalState::instance().prepareState;
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativePrepare(JNIEnv* jni, jclass, jobject assetsJ, jobject cbJ) {
    // NDK: nao existe JNIEnv::GetAAssetManager — o helper oficial e AAssetManager_fromJava
    AAssetManager* am = AAssetManager_fromJava(jni, assetsJ);
    jclass cbClass = jni->GetObjectClass(cbJ);
    jmethodID mid = jni->GetMethodID(cbClass, "onProgress", "(JJ)V");
    int rc = Prepare::run(am, [jni, cbJ, mid](long long done, long long total) {
        jni->CallVoidMethod(cbJ, mid, (jlong)done, (jlong)total);
    });
    return rc;
}

JNIEXPORT jstring JNICALL
Java_com_novawin_core_Runtime_nativeInfo(JNIEnv* jni, jclass) {
    auto& g = GlobalState::instance();
    JsonValue v;
    v.set("wine", g.wineVersion.empty() ? "desconhecida" : g.wineVersion);
    v.set("wine32", g.wine32Available);
    v.set("box64", runNativeCapture(g.rootfsDir + "/usr/local/bin/box64", {"--version"}));
    v.set("box86", runNativeCapture(g.rootfsDir + "/usr/local/bin/box86", {"--version"}));
    SDL_version linked;
    SDL_VERSION(&linked);
    char sdl[32];
    snprintf(sdl, sizeof(sdl), "%d.%d.%d", linked.major, linked.minor, linked.patch);
    v.set("sdl", sdl);
    v.set("cores", g.cores);
    v.set("rootfsReady", Prepare::rootfsReady());
    return jni->NewStringUTF(jsonDump(v, false).c_str());
}

JNIEXPORT jstring JNICALL
Java_com_novawin_core_Runtime_nativeEnvsList(JNIEnv* jni, jclass) {
    auto& g = GlobalState::instance();
    std::string text;
    if (!readWholeFile(g.dataDir + "/environments.json", text)) text = "[]";
    return jni->NewStringUTF(text.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_novawin_core_Runtime_nativeEnvCreate(JNIEnv* jni, jclass, jstring cfgJ) {
    const char* p = jni->GetStringUTFChars(cfgJ, nullptr);
    std::string out = EnvManager::create(p);
    jni->ReleaseStringUTFChars(cfgJ, p);
    return jni->NewStringUTF(out.c_str());
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativeEnvDelete(JNIEnv* jni, jclass, jstring idJ) {
    const char* p = jni->GetStringUTFChars(idJ, nullptr);
    std::string id = p;
    jni->ReleaseStringUTFChars(idJ, p);
    Launcher::instance().killSession(id);
    BridgeManager::instance().release(id);
    EnvManager::remove(id);
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativeEnvPrefixInit(JNIEnv* jni, jclass, jstring idJ, jobject cbJ) {
    const char* p = jni->GetStringUTFChars(idJ, nullptr);
    std::string id = p;
    jni->ReleaseStringUTFChars(idJ, p);
    int rc;
    if (cbJ) {
        jclass cbClass = jni->GetObjectClass(cbJ);
        jmethodID mid = jni->GetMethodID(cbClass, "onLine", "(Ljava/lang/String;)V");
        rc = EnvManager::initPrefix(id, [jni, cbJ, mid](const std::string& line) {
            jni->CallVoidMethod(cbJ, mid, jni->NewStringUTF(line.c_str()));
        });
    } else {
        rc = EnvManager::initPrefix(id, nullptr);
    }
    return rc;
}

JNIEXPORT jstring JNICALL
Java_com_novawin_core_Runtime_nativeSessionStart(JNIEnv* jni, jclass,
                                                 jstring envIdJ, jstring targetJ,
                                                 jstring optsJ) {
    const char* e = jni->GetStringUTFChars(envIdJ, nullptr);
    const char* t = jni->GetStringUTFChars(targetJ, nullptr);
    const char* o = jni->GetStringUTFChars(optsJ, nullptr);
    std::string envId = e, target = t, opts = o;
    jni->ReleaseStringUTFChars(envIdJ, e);
    jni->ReleaseStringUTFChars(targetJ, t);
    jni->ReleaseStringUTFChars(optsJ, o);
    // grava as opções (aplicadas de verdade no env do guest)
    JsonValue ov = jsonParse(opts);
    for (auto& kv : ov.obj)
        GlobalState::instance().setOpt(kv.first,
            kv.second.type == JsonValue::NUM ? std::to_string((long long)kv.second.num)
                                             : kv.second.type == JsonValue::BOOL ? (kv.second.b ? "1" : "0")
                                             : kv.second.str);
    std::string out = Launcher::instance().startSession(envId, target, opts);
    return jni->NewStringUTF(out.c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_novawin_core_Runtime_nativeSessionAlive(JNIEnv*, jclass, jstring envIdJ) {
    JNIEnv* jni = env();
    const char* e = jni->GetStringUTFChars(envIdJ, nullptr);
    bool a = Launcher::instance().sessionAlive(e);
    jni->ReleaseStringUTFChars(envIdJ, e);
    return a;
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativeSessionKill(JNIEnv* jni, jclass, jstring envIdJ) {
    const char* p = jni->GetStringUTFChars(envIdJ, nullptr);
    std::string id = p;
    jni->ReleaseStringUTFChars(envIdJ, p);
    Launcher::instance().killSession(id);
    BridgeManager::instance().release(id);
    return 0;
}

JNIEXPORT jstring JNICALL
Java_com_novawin_core_Runtime_nativeLaunchInSession(JNIEnv* jni, jclass,
                                                    jstring envIdJ, jstring pathJ) {
    const char* e = jni->GetStringUTFChars(envIdJ, nullptr);
    const char* p2 = jni->GetStringUTFChars(pathJ, nullptr);
    std::string out = Launcher::instance().launchInSession(e, p2);
    jni->ReleaseStringUTFChars(envIdJ, e);
    jni->ReleaseStringUTFChars(pathJ, p2);
    return jni->NewStringUTF(out.c_str());
}

// ---------------- bridge ----------------

JNIEXPORT jstring JNICALL
Java_com_novawin_core_Runtime_nativeBridgeConnect(JNIEnv* jni, jclass, jstring envIdJ) {
    const char* e = jni->GetStringUTFChars(envIdJ, nullptr);
    std::string id = e;
    jni->ReleaseStringUTFChars(envIdJ, e);
    Launcher::instance().reap();
    std::string out = BridgeManager::instance().bridge(id)->connect(id);
    return jni->NewStringUTF(out.c_str());
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativeBridgeDisconnect(JNIEnv* jni, jclass, jstring envIdJ) {
    const char* e = jni->GetStringUTFChars(envIdJ, nullptr);
    std::string id = e;
    jni->ReleaseStringUTFChars(envIdJ, e);
    BridgeManager::instance().release(id);
    return 0;
}

JNIEXPORT jlong JNICALL
Java_com_novawin_core_Runtime_nativeBridgeFrameSeq(JNIEnv* jni, jclass, jstring envIdJ) {
    const char* e = jni->GetStringUTFChars(envIdJ, nullptr);
    std::string id = e;
    jni->ReleaseStringUTFChars(envIdJ, e);
    auto* b = BridgeManager::instance().bridge(id);
    b->pump();
    return (jlong)b->frameSeq;
}

JNIEXPORT jstring JNICALL
Java_com_novawin_core_Runtime_nativeBridgeShmName(JNIEnv* jni, jclass, jstring envIdJ) {
    const char* e = jni->GetStringUTFChars(envIdJ, nullptr);
    std::string id = e;
    jni->ReleaseStringUTFChars(envIdJ, e);
    auto* b = BridgeManager::instance().bridge(id);
    return jni->NewStringUTF(b->shmName.c_str());
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativeBridgeScreenSize(JNIEnv* jni, jclass, jstring envIdJ) {
    const char* e = jni->GetStringUTFChars(envIdJ, nullptr);
    std::string id = e;
    jni->ReleaseStringUTFChars(envIdJ, e);
    auto* b = BridgeManager::instance().bridge(id);
    b->pump();
    return (b->width << 16) | (b->height & 0xFFFF);
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativeBridgePointer(JNIEnv* jni, jclass, jstring envIdJ,
                                                  jfloat x, jfloat y, jint buttons) {
    const char* e = jni->GetStringUTFChars(envIdJ, nullptr);
    std::string id = e;
    jni->ReleaseStringUTFChars(envIdJ, e);
    BridgeManager::instance().bridge(id)->pointer(x, y, (int)buttons);
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativeBridgeKey(JNIEnv* jni, jclass, jstring envIdJ,
                                              jstring keyJ, jboolean down) {
    const char* e = jni->GetStringUTFChars(envIdJ, nullptr);
    const char* k = jni->GetStringUTFChars(keyJ, nullptr);
    std::string id = e, key = k;
    jni->ReleaseStringUTFChars(envIdJ, e);
    jni->ReleaseStringUTFChars(keyJ, k);
    BridgeManager::instance().bridge(id)->key(key, (bool)down);
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativeBridgeIme(JNIEnv* jni, jclass, jstring envIdJ,
                                              jstring textJ) {
    const char* e = jni->GetStringUTFChars(envIdJ, nullptr);
    const char* t = jni->GetStringUTFChars(textJ, nullptr);
    std::string id = e, text = t;
    jni->ReleaseStringUTFChars(envIdJ, e);
    jni->ReleaseStringUTFChars(textJ, t);
    BridgeManager::instance().bridge(id)->ime(text);
    return 0;
}

JNIEXPORT jstring JNICALL
Java_com_novawin_core_Runtime_nativeBridgeWindows(JNIEnv* jni, jclass, jstring envIdJ) {
    const char* e = jni->GetStringUTFChars(envIdJ, nullptr);
    std::string id = e;
    jni->ReleaseStringUTFChars(envIdJ, e);
    auto* b = BridgeManager::instance().bridge(id);
    b->pump();
    return jni->NewStringUTF(jsonDump(JsonValue{JsonValue::ARR, false, 0, {}, b->windows, {}}, false).c_str());
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativeBridgeFocus(JNIEnv* jni, jclass, jstring envIdJ, jint winId) {
    const char* e = jni->GetStringUTFChars(envIdJ, nullptr);
    std::string id = e;
    jni->ReleaseStringUTFChars(envIdJ, e);
    BridgeManager::instance().bridge(id)->focus((int)winId);
    return 0;
}

JNIEXPORT jstring JNICALL
Java_com_novawin_core_Runtime_nativeBridgeProcesses(JNIEnv* jni, jclass, jstring envIdJ) {
    (void)envIdJ;
    return jni->NewStringUTF(procsList().c_str());
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativeBridgeKill(JNIEnv* jni, jclass, jstring envIdJ, jint pid) {
    const char* e = jni->GetStringUTFChars(envIdJ, nullptr);
    std::string id = e;
    jni->ReleaseStringUTFChars(envIdJ, e);
    BridgeManager::instance().bridge(id)->killPid((int)pid);
    return 0;
}

JNIEXPORT jstring JNICALL
Java_com_novawin_core_Runtime_nativeBridgeLog(JNIEnv* jni, jclass, jstring envIdJ, jint maxLines) {
    const char* e = jni->GetStringUTFChars(envIdJ, nullptr);
    std::string id = e;
    jni->ReleaseStringUTFChars(envIdJ, e);
    std::string out = BridgeManager::instance().combinedLog(id, (int)maxLines);
    return jni->NewStringUTF(out.c_str());
}

// ---------------- filesystem ----------------

JNIEXPORT jstring JNICALL
Java_com_novawin_core_Runtime_nativeFsList(JNIEnv* jni, jclass, jstring pathJ) {
    const char* p = jni->GetStringUTFChars(pathJ, nullptr);
    std::string path = p;
    jni->ReleaseStringUTFChars(pathJ, p);
    std::vector<DirEntry> entries;
    listDir(path, entries);
    JsonValue arr;
    arr.type = JsonValue::ARR;
    for (auto& e : entries) {
        JsonValue o;
        o.set("name", e.name);
        o.set("path", e.path);
        o.set("dir", e.isDirectory);
        o.set("size", (int)e.size);
        o.set("mtime", (int)e.mtime);
        arr.arr.push_back(std::move(o));
    }
    return jni->NewStringUTF(jsonDump(arr, false).c_str());
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativeFsMkdir(JNIEnv* jni, jclass, jstring pathJ) {
    const char* p = jni->GetStringUTFChars(pathJ, nullptr);
    bool ok = mkdirs(p);
    jni->ReleaseStringUTFChars(pathJ, p);
    return ok ? 0 : -1;
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativeFsDelete(JNIEnv* jni, jclass, jstring pathJ) {
    const char* p = jni->GetStringUTFChars(pathJ, nullptr);
    bool ok = deleteTree(p);
    jni->ReleaseStringUTFChars(pathJ, p);
    return ok ? 0 : -1;
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativeFsCopy(JNIEnv* jni, jclass, jstring srcJ, jstring dstJ) {
    const char* s = jni->GetStringUTFChars(srcJ, nullptr);
    const char* d = jni->GetStringUTFChars(dstJ, nullptr);
    bool ok = copyTree(s, d);
    jni->ReleaseStringUTFChars(srcJ, s);
    jni->ReleaseStringUTFChars(dstJ, d);
    return ok ? 0 : -1;
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativeFsMove(JNIEnv* jni, jclass, jstring srcJ, jstring dstJ) {
    const char* s = jni->GetStringUTFChars(srcJ, nullptr);
    const char* d = jni->GetStringUTFChars(dstJ, nullptr);
    bool ok = moveTree(s, d);
    jni->ReleaseStringUTFChars(srcJ, s);
    jni->ReleaseStringUTFChars(dstJ, d);
    return ok ? 0 : -1;
}

JNIEXPORT jstring JNICALL
Java_com_novawin_core_Runtime_nativeFsInfo(JNIEnv* jni, jclass, jstring pathJ) {
    const char* p = jni->GetStringUTFChars(pathJ, nullptr);
    std::string path = p;
    jni->ReleaseStringUTFChars(pathJ, p);
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return jni->NewStringUTF("{}");
    }
    JsonValue o;
    o.set("name", baseName(path));
    o.set("path", path);
    o.set("dir", S_ISDIR(st.st_mode));
    o.set("size", (int)st.st_size);
    o.set("mtime", (int)st.st_mtime);
    return jni->NewStringUTF(jsonDump(o, false).c_str());
}

// ---------------- processos ----------------

JNIEXPORT jstring JNICALL
Java_com_novawin_core_Runtime_nativeProcesses(JNIEnv* jni, jclass) {
    return jni->NewStringUTF(procsList().c_str());
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativeKill(JNIEnv*, jclass, jint pid) {
    return procKill((pid_t)pid);
}

JNIEXPORT jint JNICALL
Java_com_novawin_core_Runtime_nativeSetAffinity(JNIEnv*, jclass, jint pid, jint mask) {
    return procSetAffinity((pid_t)pid, (int)mask);
}

} // extern "C"
