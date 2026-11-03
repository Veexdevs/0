// NovaWin — launcher: gestão real de processos guest (Box64/Box86 + Wine)
#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <sys/types.h>
#include <string>
#include <vector>

namespace nw {

struct GuestEnv {
    std::string rootfs;      // dir absoluto do rootfs
    std::string envDir;      // rootfs/envs/<id>
    std::string prefix;      // envDir/prefix
    std::string tmp;         // envDir/tmp
    int arch = 0;            // 0 = win64/box64, 1 = win32/box86
    std::string wineDebug = "-all";
    int dynarec = 1;
    std::string extra;       // variáveis extras "K=V K2=V2" (raro)

    // monta o vetor envp (já termina com nullptr no uso)
    std::vector<std::string> envStrings() const;
    static std::string box64Path(const std::string& rootfs) { return rootfs + "/usr/local/bin/box64"; }
    static std::string box86Path(const std::string& rootfs) { return rootfs + "/usr/local/bin/box86"; }
    std::string boxPath() const { return arch == 1 ? box86Path(rootfs) : box64Path(rootfs); }
};

// fork + setsid + execvpe (cria novo group de processo para poder encerrar tudo)
pid_t execGuest(const std::string& boxPath, const std::vector<std::string>& argvAfterBox,
                const GuestEnv& env, const std::string& cwd);

// executa comando guest bloqueante, capturando saída (para wineboot etc.)
// callback pode ser null. Retorna código de saída (ou -1 em erro de spawn).
int runGuestCaptured(const std::string& boxPath, const std::vector<std::string>& argv,
                     const GuestEnv& env, const std::string& cwd,
                     const std::function<void(const std::string&)>& lineCb);

struct Session {
    std::string envId;
    pid_t sessionPid = -1;
    std::vector<pid_t> winePids;
    std::string resolution = "1280x720";
    bool alive() const;
};

class Launcher {
public:
    static Launcher& instance();

    // inicia a sessão do ambiente (Xvfb+openbox+bridge) e opcionalmente um alvo
    // retorna JSON string {ok, message, detail, sessionPid}
    std::string startSession(const std::string& envId, const std::string& target,
                             const std::string& optsRaw);
    // lança um programa dentro da sessão já ativa
    std::string launchInSession(const std::string& envId, const std::string& exePath);
    bool sessionAlive(const std::string& envId);
    int killSession(const std::string& envId);
    // sessão atual (se existir): pid do session leader
    pid_t sessionPid(const std::string& envId);
    // encerra e limpa sessões mortas; chamado periodicamente
    void reap();

    // ambiente (paths) de um id de ambiente; vazio se não existir
    GuestEnv envFor(const std::string& envId) const;

private:
    std::mutex mu_;
    std::map<std::string, Session> sessions_;
};

} // namespace nw
