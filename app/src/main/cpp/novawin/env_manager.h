// NovaWin — gestão de ambientes (prefixos Wine) + estado global do runtime
#pragma once

#include "util.h"

#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace nw {

/**
 * Estado global do runtime (preenchido pelo JNI init).
 * `currentOpts` carrega as opções da última sessão pedida (resolução,
 * renderer, dynarec, affinity, wineDebug) — aplicadas de verdade ao guest.
 */
struct GlobalState {
    static GlobalState& instance();

    std::string filesDir;
    std::string rootfsDir;
    std::string dataDir;

    // estado de preparação: 0 pendente, 1 em andamento, 2 pronto, -1 erro
    int prepareState = 0;

    mutable std::mutex mu;
    std::map<std::string, std::string> opts;

    std::string optString(const std::string& key, const std::string& def = "") const;
    int optInt(const std::string& key, int def) const;
    void setOpt(const std::string& key, const std::string& value);

    // versão do wine detectada no rootfs (dpkg/wine --version)
    std::string wineVersion;
    bool wine32Available = false;
    int cores = 4;
};

class EnvManager {
public:
    // cria o diretório do ambiente dentro do rootfs (apps, tmp, home, prefix)
    static std::string create(const std::string& jsonConfig); // JSON string
    // apaga o ambiente
    static int remove(const std::string& envId);
    // inicializa o prefixo Wine (wineboot --init) — bloqueante
    static int initPrefix(const std::string& envId, const std::function<void(const std::string&)>& lineCb);
};

/**
 * Preparação do runtime: extrai assets (rootfs.tar.gz + box64 + box86)
 * para filesDir. Chamado em thread de background.
 */
class Prepare {
public:
    // AAssetManager* vem do Kotlin
    static int run(void* assetManager, const std::function<void(long long, long long)>& progress);
    static bool rootfsReady();
};

} // namespace nw
