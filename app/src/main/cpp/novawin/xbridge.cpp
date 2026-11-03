// NovaWin — cliente do bridge guest
#include "xbridge.h"

#include <android/log.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>
#include <cstring>
#include <chrono>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NovaWin", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "NovaWin", __VA_ARGS__)

namespace nw {

namespace {
struct MsgHeader {
    uint8_t type;
    uint8_t version;
    uint16_t flags;
    uint32_t length;
    uint32_t seq;
    uint32_t arg1;
    uint32_t arg2;
}; // 24 bytes, little-endian (mesma arquitetura de ambos os lados)

bool sendAll(int fd, const void* data, size_t len) {
    const unsigned char* p = (const unsigned char*)data;
    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += n;
        len -= (size_t)n;
    }
    return true;
}
bool recvAll(int fd, void* data, size_t len) {
    unsigned char* p = (unsigned char*)data;
    while (len > 0) {
        ssize_t n = recv(fd, p, len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}
} // namespace

std::string Bridge::socketPath(const std::string& envId) {
    GuestEnv env = Launcher::instance().envFor(envId);
    return joinPath(env.tmp, ".novawin/bridge.sock");
}

std::string Bridge::connect(const std::string& envId) {
    disconnect();
    std::string sockPath = socketPath(envId);
    if (!fileExists(sockPath)) {
        return jsonDump(JsonValue().set("ok", false)
            .set("error", "socket do bridge ausente (sessão não está ativa)"), false);
    }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return jsonDump(JsonValue().set("ok", false).set("error", "socket() falhou"), false);
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(fd);
        return jsonDump(JsonValue().set("ok", false)
            .set("error", "falha ao conectar no bridge do guest"), false);
    }
    // modo não-bloqueante (pump)
    fcntl(fd, F_SETFL, O_NONBLOCK);
    this->fd = fd;

    // aguarda GW_HELLO
    std::vector<unsigned char> payload;
    MsgHeader h{};
    bool gotHello = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        struct pollfd pfd{fd, POLLIN, 0};
        int pr = poll(&pfd, 1, 100);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            if (recvMsg(h.type, h.arg1, h.arg2, payload)) {
                if (h.type == GW_HELLO) {
                    JsonValue hello = jsonParse((const char*)payload.data());
                    helloJson = (const char*)payload.data();
                    shmName = hello.getString("shm");
                    width = hello.getInt("w");
                    height = hello.getInt("h");
                    gotHello = true;
                    break;
                }
            }
        } else if (pr < 0 && errno != EINTR) {
            break;
        }
    }
    if (!gotHello) {
        disconnect();
        return jsonDump(JsonValue().set("ok", false)
            .set("error", "bridge não respondeu (HELLO)"), false);
    }
    JsonValue out;
    out.set("ok", true);
    out.set("shm", shmName);
    out.set("w", width);
    out.set("h", height);
    out.set("wine32", helloJson.find("\"wine32\":true") != std::string::npos);
    return jsonDump(out, false);
}

void Bridge::disconnect() {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
    shmName.clear();
    width = height = 0;
    frameSeq = 0;
}

bool Bridge::sendMsg(uint8_t type, uint32_t arg1, const void* payload, uint32_t len) {
    if (fd < 0) return false;
    MsgHeader h{};
    h.type = type;
    h.version = 1;
    h.flags = 0;
    h.length = len;
    h.arg1 = arg1;
    if (!sendAll(fd, &h, sizeof(h))) return false;
    if (len > 0 && !sendAll(fd, payload, len)) return false;
    return true;
}

int Bridge::recvMsg(uint8_t& type, uint32_t& arg1, uint32_t& arg2,
                    std::vector<unsigned char>& payload) {
    MsgHeader h{};
    if (!recvAll(fd, &h, sizeof(h))) return -1;
    type = h.type;
    arg1 = h.arg1;
    arg2 = h.arg2;
    payload.clear();
    if (h.length > 0) {
        if (h.length > (1u << 20)) return -1; // sanity
        payload.resize(h.length);
        if (!recvAll(fd, payload.data(), h.length)) return -1;
    }
    return 0;
}

void Bridge::pump() {
    if (fd < 0) return;
    for (int i = 0; i < 16; i++) { // limita trabalho por chamada
        struct pollfd pfd{fd, POLLIN, 0};
        int pr = poll(&pfd, 1, 0);
        if (pr <= 0) break;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            disconnect();
            return;
        }
        uint8_t type;
        uint32_t arg1, arg2;
        std::vector<unsigned char> payload;
        if (recvMsg(type, arg1, arg2, payload) != 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            disconnect();
            return;
        }
        switch (type) {
            case GW_FRAME:
                frameSeq = arg1;
                break;
            case GW_WINDOWS:
                if (!payload.empty()) {
                    JsonValue arr = jsonParse((const char*)payload.data());
                    if (arr.type == JsonValue::ARR) windows = arr.arr;
                }
                break;
            case GW_PROCS:
                if (!payload.empty()) {
                    JsonValue arr = jsonParse((const char*)payload.data());
                    if (arr.type == JsonValue::ARR) procs = arr.arr;
                }
                break;
            case GW_LOG:
                if (!payload.empty()) {
                    JsonValue v = jsonParse((const char*)payload.data());
                    std::string t = v.getString("t");
                    if (!t.empty()) {
                        logRing.push_back(t);
                        if (logRing.size() > 400) logRing.erase(logRing.begin(),
                                                                logRing.begin() + 100);
                    }
                }
                break;
            default:
                break;
        }
    }
}

void Bridge::pointer(float x, float y, int buttons) {
    struct { float x; float y; } p{x, y};
    sendMsg(GA_POINTER, (uint32_t)buttons, &p, sizeof(p));
}
void Bridge::key(const std::string& jsonKey, bool down) {
    sendMsg(GA_KEY, down ? 1 : 0, jsonKey.data(), (uint32_t)jsonKey.size());
}
void Bridge::ime(const std::string& text) {
    sendMsg(GA_IME, 0, text.data(), (uint32_t)text.size());
}
void Bridge::focus(int winId) {
    sendMsg(GA_FOCUS, (uint32_t)winId, nullptr, 0);
}
void Bridge::killPid(int pid) {
    sendMsg(GA_KILL, (uint32_t)pid, nullptr, 0);
}
void Bridge::ping() {
    sendMsg(GA_PING, 0, nullptr, 0);
}

BridgeManager& BridgeManager::instance() {
    static BridgeManager m;
    return m;
}

Bridge* BridgeManager::bridge(const std::string& envId) {
    std::lock_guard<std::mutex> lk(mu_);
    return &bridges_[envId];
}

void BridgeManager::release(const std::string& envId) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = bridges_.find(envId);
    if (it != bridges_.end()) {
        it->second.disconnect();
        bridges_.erase(it);
    }
}

std::string BridgeManager::combinedLog(const std::string& envId, int maxLines) {
    GuestEnv env = Launcher::instance().envFor(envId);
    std::vector<std::string> lines;
    auto addTail = [&](const std::string& file, const std::string& tag) {
        std::string text;
        if (readWholeFile(file, text)) {
            std::vector<std::string> ls;
            std::string cur;
            for (char c : text) {
                if (c == '\n') { ls.push_back(cur); cur.clear(); }
                else cur += c;
            }
            if (!cur.empty()) ls.push_back(cur);
            for (size_t i = ls.size(); i-- > 0;)
                lines.push_back(tag + " " + ls[i]);
        }
    };
    addTail(joinPath(env.tmp, "wine.log"), "[wine]");
    addTail(joinPath(env.tmp, "xvfb.log"), "[xvfb]");
    addTail(joinPath(env.tmp, "openbox.log"), "[openbox]");
    auto* b = bridge(envId);
    for (auto& l : b->logRing) lines.push_back("[bridge] " + l);
    if ((int)lines.size() > maxLines)
        lines.erase(lines.begin(), lines.begin() + (lines.size() - maxLines));
    std::string out;
    for (auto& l : lines) { out += l; out += "\n"; }
    return out;
}

} // namespace nw
