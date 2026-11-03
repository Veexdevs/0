// NovaWin — cliente do bridge guest (socket unix, frames via MIT-SHM)
#pragma once

#include "launcher.h"
#include "util.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace nw {

// protocolo (24 bytes, little-endian):
//   u8 type, u8 version, u16 flags, u32 length, u32 seq, u32 arg1, u32 arg2
enum {
    GW_HELLO = 1,      // guest -> app: JSON {shm, w, h, wine32, bridge}
    GW_FRAME = 2,      // guest -> app: novo frame na SHM (sem payload)
    GW_WINDOWS = 3,    // guest -> app: JSON [{id,title,active,fs}]
    GW_PROCS = 4,      // guest -> app: JSON [{pid,name}]
    GW_LOG = 5,        // guest -> app: JSON {t:"..."}
    GA_POINTER = 10,   // app -> guest: arg1=buttons, payload=f32 x,f32 y
    GA_KEY = 11,       // app -> guest: arg1=down, payload=JSON {u:cp} | {k:"Enter"}
    GA_IME = 12,       // app -> guest: payload = UTF-8
    GA_FOCUS = 13,     // app -> guest: arg1=winId
    GA_KILL = 14,      // app -> guest: arg1=pid
    GA_PING = 15,      // app -> guest: pong via GW_LOG
};

struct Bridge {
    int fd = -1;
    std::string shmName;   // ex.: nw-frame-1234
    int width = 0;
    int height = 0;
    uint32_t frameSeq = 0;
    std::string helloJson;
    std::vector<JsonValue> windows;
    std::vector<JsonValue> procs;
    std::vector<std::string> logRing;

    // conecta ao socket do bridge; retorna JSON {ok, shm, w, h, wine32}
    std::string connect(const std::string& envId);
    void disconnect();
    bool connected() const { return fd >= 0; }
    // lê mensagens disponíveis (não bloqueante); atualiza caches
    void pump();
    // frame mais recente
    bool hasFrame() const { return frameSeq != 0; }
    void clearFrame() { frameSeq = 0; }
    // envia
    void pointer(float x, float y, int buttons);
    void key(const std::string& jsonKey, bool down);
    void ime(const std::string& text);
    void focus(int winId);
    void killPid(int pid);
    void ping();
    // caminho do socket para o ambiente
    static std::string socketPath(const std::string& envId);

private:
    bool sendMsg(uint8_t type, uint32_t arg1, const void* payload, uint32_t len);
    int recvMsg(uint8_t& type, uint32_t& arg1, uint32_t& arg2,
                std::vector<unsigned char>& payload);
};

class BridgeManager {
public:
    static BridgeManager& instance();
    Bridge* bridge(const std::string& envId);
    void release(const std::string& envId);
    // log combinado (bridge + wine.log) para a tela de Configurações
    std::string combinedLog(const std::string& envId, int maxLines);

private:
    std::mutex mu_;
    std::map<std::string, Bridge> bridges_;
};

} // namespace nw
