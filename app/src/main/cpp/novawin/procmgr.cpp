// NovaWin — gestão de processos (scan /proc + affinity real)
#include "util.h"

#include <android/log.h>
#include <sched.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <cstring>
#include <dirent.h>

namespace nw {

// lista processos guest (descendentes do app) e do sistema relevantes
static std::string readCmdline(pid_t pid) {
    std::string path = "/proc/" + std::to_string(pid) + "/cmdline";
    std::string raw;
    if (!readWholeFile(path, raw)) return "";
    std::string first;
    for (char c : raw) {
        if (c == 0) break;
        first += c;
    }
    return first;
}

static bool isGuestProcess(const std::string& cmdline) {
    if (cmdline.empty()) return false;
    if (cmdline.find("wine") != std::string::npos) return true;
    if (cmdline.find(".exe") != std::string::npos) return true;
    if (cmdline.find("novawin-session") != std::string::npos) return true;
    if (cmdline.find("Xvfb") != std::string::npos) return true;
    if (cmdline.find("openbox") != std::string::npos) return true;
    if (cmdline.find("novawin-bridge") != std::string::npos) return true;
    if (cmdline.find("box64") != std::string::npos) return true;
    if (cmdline.find("box86") != std::string::npos) return true;
    return false;
}

std::string procsList() {
    JsonValue arr;
    arr.type = JsonValue::ARR;
    DIR* d = opendir("/proc");
    if (!d) return jsonDump(arr, false);
    struct dirent* ent;
    pid_t self = getpid();
    while ((ent = readdir(d)) != nullptr) {
        const char* n = ent->d_name;
        bool num = true;
        for (const char* c = n; *c; c++) if (*c < '0' || *c > '9') { num = false; break; }
        if (!num) continue;
        pid_t pid = (pid_t)atoi(n);
        if (pid == 0 || pid == self) continue;
        std::string statPath = std::string("/proc/") + n + "/stat";
        std::string statText;
        if (!readWholeFile(statPath, statText)) continue;
        // pid (comm) state ppid ...
        size_t lp = statText.find('(');
        size_t rp = statText.rfind(')');
        std::string comm;
        std::string state = "?";
        if (lp != std::string::npos && rp != std::string::npos && rp > lp) {
            comm = statText.substr(lp + 1, rp - lp - 1);
            if (rp + 2 < statText.size()) state = std::string(1, statText[rp + 2]);
        }
        std::string cmdline = readCmdline(pid);
        bool guest = isGuestProcess(cmdline);
        // apenas processos guest (do nosso ambiente)
        if (!guest) continue;
        JsonValue o;
        o.set("pid", (int)pid);
        o.set("name", comm);
        o.set("state", state);
        o.set("guest", true);
        arr.arr.push_back(std::move(o));
    }
    closedir(d);
    return jsonDump(arr, false);
}

int procKill(pid_t pid) {
    if (pid <= 0) return -1;
    if (kill(pid, SIGTERM) != 0) return -1;
    // espera curta e força se preciso
    for (int i = 0; i < 10; i++) {
        if (kill(pid, 0) != 0) return 0;
        struct timespec ts{0, 100 * 1000 * 1000};
        nanosleep(&ts, nullptr);
    }
    kill(pid, SIGKILL);
    return 0;
}

int procSetAffinity(pid_t pid, int mask) {
    if (pid <= 0 || mask <= 0) return -1;
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int i = 0; i < 32; i++)
        if ((mask >> i) & 1) CPU_SET(i, &set);
    return sched_setaffinity(pid, sizeof(set), &set);
}

int procCpuCount() {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

} // namespace nw
