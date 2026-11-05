// NovaWin — launcher: spawn real de processos guest via Box64/Box86
#include "launcher.h"

#include "env_manager.h"
#include "util.h"

#include <android/log.h>
#include <sched.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <chrono>
#include <thread>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NovaWin", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "NovaWin", __VA_ARGS__)

namespace nw {

std::vector<std::string> GuestEnv::envStrings() const {
    const std::string libdirs =
        rootfs + "/usr/lib/x86_64-linux-gnu:" +
        rootfs + "/lib/x86_64-linux-gnu:" +
        rootfs + "/usr/lib:" +
        rootfs + "/lib";
    const std::string xsock = tmp + "/.novawin/x11";
    std::vector<std::string> e;
    e.push_back("HOME=" + envDir + "/home");
    e.push_back("USER=novawin");
    e.push_back("WINEPREFIX=" + prefix);
    e.push_back(arch == 1 ? "WINEARCH=win32" : "WINEARCH=win64");
    e.push_back("WINEDEBUG=" + wineDebug);
    e.push_back("WINEDLLOVERRIDES=mscoree,mshtml=");
    e.push_back("DISPLAY=127.0.0.1:0");
    e.push_back("X11_SOCKET_DIR=" + xsock);
    e.push_back("PATH=" + rootfs + "/usr/local/bin:" + rootfs + "/usr/bin:" + rootfs + "/bin");
    e.push_back("LD_LIBRARY_PATH=" + libdirs);
    e.push_back("BOX64_LD_LIBRARY_PATH=" + libdirs);
    e.push_back("BOX86_LD_LIBRARY_PATH=" + libdirs);
    e.push_back(std::string("BOX64_DYNAREC=") + std::to_string(dynarec));
    e.push_back(std::string("BOX86_DYNAREC=") + std::to_string(dynarec));
    e.push_back("TMPDIR=" + tmp);
    e.push_back("LANG=en_US.UTF-8");
    e.push_back("LC_ALL=");
    e.push_back("NW_TMP=" + tmp);
    e.push_back("NW_ENV_DIR=" + envDir);
    e.push_back(arch == 1 ? "NW_WINE=wine32" : "NW_WINE=wine64");
    e.push_back("XDG_CONFIG_HOME=" + envDir + "/home/.config");
    e.push_back("XDG_CACHE_HOME=" + envDir + "/home/.cache");
    e.push_back("XDG_DATA_HOME=" + envDir + "/home/.local/share");
    if (!extra.empty()) {
        // extras simples "K=V K2=V2"
        std::string s = extra;
        size_t i = 0;
        while (i < s.size()) {
            size_t sp = s.find(' ', i);
            std::string kv = (sp == std::string::npos) ? s.substr(i) : s.substr(i, sp - i);
            if (!kv.empty()) e.push_back(kv);
            if (sp == std::string::npos) break;
            i = sp + 1;
        }
    }
    return e;
}

pid_t execGuest(const std::string& boxPath, const std::vector<std::string>& argvAfterBox,
                const GuestEnv& env, const std::string& cwd) {
    std::vector<std::string> fullArgv;
    fullArgv.push_back(boxPath);
    for (auto& a : argvAfterBox) fullArgv.push_back(a);

    std::vector<std::string> envStr = env.envStrings();
    envStr.push_back(""); // termina com vazio

    std::vector<char*> argv;
    for (auto& a : fullArgv) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    std::vector<char*> envp;
    for (auto& s : envStr) envp.push_back(const_cast<char*>(s.c_str()));
    envp.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        LOGE("fork falhou: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        // filho: apenas chamadas seguras e exec
        setsid();
        if (!cwd.empty()) chdir(cwd.c_str());
        // redireciona fd 0/1/2 para /dev/null (o app lê logs via arquivos)
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, 0);
            dup2(devnull, 1);
            dup2(devnull, 2);
            if (devnull > 2) close(devnull);
        }
        execve(boxPath.c_str(), argv.data(), envp.data());
        _exit(127);
    }
    return pid;
}

int runGuestCaptured(const std::string& boxPath, const std::vector<std::string>& argv,
                     const GuestEnv& env, const std::string& cwd,
                     const std::function<void(const std::string&)>& lineCb) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    std::vector<std::string> fullArgv;
    fullArgv.push_back(boxPath);
    for (auto& a : argv) fullArgv.push_back(a);
    std::vector<std::string> envStr = env.envStrings();
    envStr.push_back("");
    std::vector<char*> argvp;
    for (auto& a : fullArgv) argvp.push_back(const_cast<char*>(a.c_str()));
    argvp.push_back(nullptr);
    std::vector<char*> envp;
    for (auto& s : envStr) envp.push_back(const_cast<char*>(s.c_str()));
    envp.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        close(pipefd[0]);
        setsid();
        if (!cwd.empty()) chdir(cwd.c_str());
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, 0);
        dup2(pipefd[1], 1);
        dup2(pipefd[1], 2);
        if (devnull >= 0 && devnull > 2) close(devnull);
        close(pipefd[1]);
        execve(boxPath.c_str(), argvp.data(), envp.data());
        _exit(127);
    }
    close(pipefd[1]);

    // lê saída em threads para evitar deadlock em pipes grandes
    std::string buf;
    bool gotError = false;
    {
        std::vector<char> chunk(8192);
        std::string line;
        for (;;) {
            ssize_t n = read(pipefd[0], chunk.data(), chunk.size());
            if (n < 0) { if (errno == EINTR) continue; break; }
            if (n == 0) break;
            for (ssize_t i = 0; i < n; i++) {
                if (chunk[i] == '\n') {
                    if (lineCb) lineCb(line);
                    line.clear();
                } else {
                    line += chunk[i];
                    if (line.size() > 8192) {
                        if (lineCb) lineCb(line);
                        line.clear();
                    }
                }
            }
        }
        if (!line.empty() && lineCb) lineCb(line);
        line.clear();
    }
    close(pipefd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) { gotError = true; break; }
    }
    (void)gotError;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

bool Session::alive() const {
    return sessionPid > 0 && kill(sessionPid, 0) == 0;
}

Launcher& Launcher::instance() {
    static Launcher inst;
    return inst;
}

GuestEnv Launcher::envFor(const std::string& envId) const {
    GuestEnv e;
    std::string dirName = envId;
    // lê o dirName do registro de ambientes (Store em Kotlin)
    JsonValue regs = jsonParse("[]");
    std::string text;
    if (readWholeFile(GlobalState::instance().dataDir + "/environments.json", text)) {
        JsonValue arr = jsonParse(text);
        if (arr.type == JsonValue::ARR) {
            for (auto& v : arr.arr) {
                if (v.getString("id") == envId) {
                    dirName = v.getString("dirName", envId);
                    e.arch = v.getInt("arch", 0);
                    break;
                }
            }
        }
    }
    e.rootfs = GlobalState::instance().rootfsDir;
    e.envDir = joinPath(joinPath(e.rootfs, "envs"), dirName);
    e.prefix = joinPath(e.envDir, "prefix");
    e.tmp = joinPath(e.envDir, "tmp");
    e.arch = e.arch; // (mantém)
    return e;
}

std::string Launcher::startSession(const std::string& envId, const std::string& target,
                                   const std::string& /*optsRaw*/) {
    // opts é lido do settings pelo JNI; aqui usamos o que foi passado em GlobalState
    GuestEnv env = envFor(envId);
    if (!isDir(env.envDir)) {
        return jsonDump(JsonValue().set("ok", false)
            .set("message", "Ambiente não existe")
            .set("detail", "O diretório do ambiente não foi encontrado no rootfs."), false);
    }
    env.wineDebug = GlobalState::instance().optString("wineDebug");
    env.dynarec = GlobalState::instance().optInt("dynarec", 1);
    std::string resolution = GlobalState::instance().optString("resolution", "1280x720");
    int affinity = GlobalState::instance().optInt("affinity", -1);

    {
        std::lock_guard<std::mutex> lk(mu_);
        // encerra sessão existente
        if (sessions_.count(envId)) killSession(envId);
    }

    // diretórios de runtime
    mkdirs(env.tmp);
    mkdirs(joinPath(env.tmp, ".novawin"));
    mkdirs(joinPath(env.envDir, "home"));
    writeWholeFile(joinPath(env.tmp, "wine.log"), "");

    // prepara o ambiente para o script (NW_* já vêm no envp)
    std::vector<std::string> scriptArgs;
    scriptArgs.push_back("/usr/local/bin/novawin-session.sh");
    scriptArgs.push_back(resolution);
    if (!target.empty()) scriptArgs.push_back(target);

    // afinação de CPU: aplicada após o spawn (no processo filho)
    // (o kernel permite setar affinity do próprio grupo após setsid;
    //  aqui aplicamos no pai para o session leader)
    pid_t pid = execGuest(env.boxPath(), scriptArgs, env, env.rootfs);
    if (pid <= 0) {
        return jsonDump(JsonValue().set("ok", false)
            .set("message", "Falha ao iniciar a sessão guest")
            .set("detail", "Não foi possível criar o processo (fork/exec). "
                           "Verifique se o runtime está preparado."), false);
    }
    if (affinity > 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        for (int i = 0; i < 16; i++)
            if ((affinity >> i) & 1) CPU_SET(i, &set);
        sched_setaffinity(pid, sizeof(set), &set);
    }

    Session s;
    s.envId = envId;
    s.sessionPid = pid;
    s.resolution = resolution;
    {
        std::lock_guard<std::mutex> lk(mu_);
        sessions_[envId] = s;
    }

    // aguarda o bridge criar o socket (Xvfb + openbox + bridge no guest)
    std::string sockPath = joinPath(env.tmp, ".novawin/bridge.sock");
    bool ready = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    while (std::chrono::steady_clock::now() < deadline) {
        reap();
        if (!s.alive()) break;
        if (fileExists(sockPath)) {
            // tenta conectar (o socket existe mas o bridge pode ainda não estar listen)
            int fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (fd >= 0) {
                struct sockaddr_un addr;
                memset(&addr, 0, sizeof(addr));
                addr.sun_family = AF_UNIX;
                strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);
                if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                    close(fd);
                    ready = true;
                    break;
                }
                close(fd);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    if (!ready) {
        std::string log;
        readWholeFile(joinPath(env.tmp, "bridge.log"), log);
        readWholeFile(joinPath(env.tmp, "xvfb.log"), log); // concatena (xvfb após bridge)
        killSession(envId);
        return jsonDump(JsonValue().set("ok", false)
            .set("message", "O ambiente Windows não respondeu em 90s")
            .set("detail", "O bridge guest não ficou disponível. Log: " +
                           (log.empty() ? "vazio" : log.substr(0, 512))), false);
    }
    return jsonDump(JsonValue().set("ok", true)
        .set("message", "Sessão iniciada")
        .set("sessionPid", (int)pid)
        .set("pid", (int)pid), false);
}

std::string Launcher::launchInSession(const std::string& envId, const std::string& exePath) {
    GuestEnv env = envFor(envId);
    env.wineDebug = GlobalState::instance().optString("wineDebug");
    env.dynarec = GlobalState::instance().optInt("dynarec", 1);

    std::vector<std::string> argv;
    argv.push_back(env.arch == 1 ? "wine32" : "wine64");
    argv.push_back(exePath);
    pid_t pid = execGuest(env.boxPath(), argv, env, env.rootfs);
    if (pid <= 0) {
        return jsonDump(JsonValue().set("ok", false)
            .set("message", "Falha ao lançar o programa"), false);
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = sessions_.find(envId);
        if (it != sessions_.end()) it->second.winePids.push_back(pid);
    }
    return jsonDump(JsonValue().set("ok", true)
        .set("message", "Programa lançado")
        .set("pid", (int)pid), false);
}

bool Launcher::sessionAlive(const std::string& envId) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = sessions_.find(envId);
    if (it == sessions_.end()) return false;
    return it->second.alive();
}

int Launcher::killSession(const std::string& envId) {
    Session s;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = sessions_.find(envId);
        if (it == sessions_.end()) return 0;
        s = it->second;
    }
    if (s.sessionPid > 0) {
        kill((pid_t)-s.sessionPid, SIGTERM); // grupo (setsid => pgid == pid)
        for (auto p : s.winePids) kill(p, SIGTERM);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    if (s.sessionPid > 0 && kill(s.sessionPid, 0) == 0) {
        kill((pid_t)-s.sessionPid, SIGKILL);
        for (auto p : s.winePids) kill(p, SIGKILL);
    }
    std::lock_guard<std::mutex> lk(mu_);
    sessions_.erase(envId);
    return 0;
}

pid_t Launcher::sessionPid(const std::string& envId) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = sessions_.find(envId);
    return it == sessions_.end() ? -1 : it->second.sessionPid;
}

void Launcher::reap() {
    std::vector<pid_t> toCheck;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& kv : sessions_) {
            toCheck.push_back(kv.second.sessionPid);
            for (auto p : kv.second.winePids) toCheck.push_back(p);
        }
    }
    for (pid_t p : toCheck) {
        int status = 0;
        while (waitpid(p, &status, WNOHANG) > 0) {
            std::lock_guard<std::mutex> lk(mu_);
            for (auto& kv : sessions_) {
                for (size_t i = 0; i < kv.second.winePids.size(); i++) {
                    if (kv.second.winePids[i] == p) {
                        kv.second.winePids.erase(kv.second.winePids.begin() + i);
                        break;
                    }
                }
            }
            break;
        }
    }
}

} // namespace nw
