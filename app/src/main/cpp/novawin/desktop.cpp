// NovaWin — "Ambiente Windows": janela SDL2 que exibe o desktop do guest
// (Xvfb) via frames em MIT-SHM + taskbar + gerenciamento de janelas/processos
// + entrada (toque/mouse/teclado físico/IME virtual).

#include <SDL.h>
#include <SDL_ttf.h>

#include "env_manager.h"
#include "xbridge.h"

#include <android/log.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

using namespace nw;

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NovaWin", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "NovaWin", __VA_ARGS__)

namespace {

struct Config {
    std::string filesDir;
    std::string envId;
    int gw = 1280, gh = 720;
    bool accelerated = true;
    int affinity = -1;
    int dynarec = 1;
    std::string wineDebug = "-all";
    double pointerSpeed = 1.0;
    bool showTouch = true;
    bool virtualKeyboard = true;
    bool hardwareKeyboard = true;
};

Config g_cfg;

Config loadConfig(const char* filesDir) {
    Config c;
    c.filesDir = filesDir;
    std::string base = filesDir;
    std::string text;
    if (readWholeFile(base + "/desktop_config.json", text)) {
        JsonValue j = jsonParse(text);
        c.envId = j.getString("envId");
        std::string res = j.getString("resolution", "1280x720");
        c.gw = atoi(res.c_str());
        size_t x = res.find('x');
        c.gh = x != std::string::npos ? atoi(res.c_str() + x + 1) : 720;
        c.accelerated = j.getString("renderer", "accelerated") == "accelerated";
        c.affinity = j.getInt("affinity", -1);
        c.dynarec = j.getInt("dynarec", 1);
        c.wineDebug = j.getString("wineDebug", "-all");
        if (readWholeFile(base + "/data/settings.json", text)) {
            JsonValue s = jsonParse(text);
            c.pointerSpeed = s.getInt("pointerSpeed", 100) / 100.0;
            c.showTouch = s.getBool("showTouch", true);
            c.virtualKeyboard = s.getBool("virtualKeyboard", true);
            c.hardwareKeyboard = s.getBool("hardwareKeyboard", true);
        }
    }
    return c;
}

constexpr int TB_H = 48;

struct Rect { float x, y, w, h; };
bool rectHit(const Rect& r, float px, float py) {
    return px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h;
}

void drawRoundedRect(SDL_Renderer* r, float x, float y, float w, float h, float rad,
                     SDL_Color c) {
    if (rad > w / 2) rad = w / 2;
    if (rad > h / 2) rad = h / 2;
    if (rad < 1) {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_Rect rr{(int)x, (int)y, (int)w, (int)h};
        SDL_RenderFillRect(r, &rr);
        return;
    }
    const float PI = 3.14159265f;
    const int SEG = 10;
    std::vector<SDL_Vertex> v;
    auto push = [&](float px, float py) {
        SDL_Vertex vt{};
        vt.position = {px, py};
        vt.color = {c.r, c.g, c.b, c.a};
        vt.tex_coord = {0, 0};
        v.push_back(vt);
    };
    auto arc = [&](float cx, float cy, float a0, float a1) {
        for (int i = 0; i <= SEG; i++) {
            float a = PI * (a0 + (a1 - a0) * i / SEG);
            push(cx + rad * cosf(a), cy + rad * sinf(a));
        }
    };
    arc(x + rad, y + rad, 1.0f, 1.5f);       // superior esquerda
    arc(x + w - rad, y + rad, 1.5f, 2.0f);   // superior direita
    arc(x + w - rad, y + h - rad, 2.0f, 2.5f); // inferior direita
    arc(x + rad, y + h - rad, 2.5f, 3.0f);   // inferior esquerda
    int perimeter = (int)v.size();
    SDL_Vertex center{};
    center.position = {x + w / 2, y + h / 2};
    center.color = {c.r, c.g, c.b, c.a};
    center.tex_coord = {0, 0};
    v.push_back(center);
    int centerIdx = perimeter;
    std::vector<int> idx;
    for (int i = 0; i < perimeter; i++) {
        idx.push_back(centerIdx);
        idx.push_back(i);
        idx.push_back((i + 1) % perimeter);
    }
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

struct Glyphs {
    static void fillR(SDL_Renderer* r, int x, int y, int w, int h) {
        SDL_Rect rc{x, y, w, h};
        SDL_RenderFillRect(r, &rc);
    }
    static void back(SDL_Renderer* r, float x, float y, float s) {
        int cx = (int)x, cy = (int)y, w = (int)s;
        SDL_SetRenderDrawColor(r, 200, 210, 230, 255);
        SDL_Rect body{cx + w / 4, cy + w / 2 - 1, w - w / 4, 2};
        SDL_RenderDrawRect(r, &body);
        SDL_RenderDrawLine(r, cx + w / 4, cy + w / 2, cx, cy + w / 4);
        SDL_RenderDrawLine(r, cx + w / 4, cy + w / 2, cx, cy + w - w / 4);
    }
    static void keyboard(SDL_Renderer* r, float x, float y, float s) {
        int cx = (int)x, cy = (int)y, w = (int)s;
        SDL_SetRenderDrawColor(r, 200, 210, 230, 255);
        SDL_Rect body{cx, cy, w, (int)(w * 0.6f)};
        SDL_RenderDrawRect(r, &body);
        for (int row = 0; row < 2; row++)
            for (int col = 0; col < 4; col++)
                fillR(r, cx + 3 + col * (w - 6) / 4, cy + 3 + row * 9, (w - 10) / 4 - 1, 5);
        fillR(r, cx + w / 4, cy + (int)(w * 0.6f) + 5, w / 2, 3);
    }
    static void list(SDL_Renderer* r, float x, float y, float s) {
        int cx = (int)x, cy = (int)y, w = (int)s;
        SDL_SetRenderDrawColor(r, 200, 210, 230, 255);
        for (int i = 0; i < 3; i++)
            fillR(r, cx, cy + 3 + i * (w - 6) / 3, w, 2);
    }
    static void power(SDL_Renderer* r, float x, float y, float s) {
        int cx = (int)x, cy = (int)y, w = (int)s;
        SDL_SetRenderDrawColor(r, 255, 90, 80, 255);
        SDL_Rect box{cx, cy + 3, w, w - 6};
        SDL_RenderDrawRect(r, &box);
        SDL_SetRenderDrawColor(r, 21, 27, 43, 255);
        SDL_RenderDrawLine(r, cx + w / 4, cy + 3, cx + w - w / 4, cy + 3);
        SDL_SetRenderDrawColor(r, 255, 90, 80, 255);
        SDL_RenderDrawLine(r, cx + w / 2, cy, cx + w / 2, cy + w / 2);
    }
};

} // namespace

// ---------------- mapeamento de teclas ----------------
static std::string sdlKeyToBridgeKey(SDL_Keysym* ks, bool* isChar) {
    *isChar = false;
    static const struct { SDL_Keycode k; const char* n; } map[] = {
        {SDLK_RETURN, "Enter"}, {SDLK_KP_ENTER, "Enter"}, {SDLK_SPACE, "Space"},
        {SDLK_BACKSPACE, "BackSpace"}, {SDLK_TAB, "Tab"}, {SDLK_ESCAPE, "Esc"},
        {SDLK_DELETE, "Delete"}, {SDLK_INSERT, "Insert"},
        {SDLK_LEFT, "Left"}, {SDLK_RIGHT, "Right"}, {SDLK_UP, "Up"}, {SDLK_DOWN, "Down"},
        {SDLK_HOME, "Home"}, {SDLK_END, "End"}, {SDLK_PAGEUP, "Page_Up"},
        {SDLK_PAGEDOWN, "Page_Down"}, {SDLK_CAPSLOCK, "Caps_Lock"},
        {SDLK_PRINTSCREEN, "Print"},
        {SDLK_LSHIFT, "Shift"}, {SDLK_RSHIFT, "Shift"},
        {SDLK_LCTRL, "Ctrl"}, {SDLK_RCTRL, "Ctrl"},
        {SDLK_LALT, "Alt"}, {SDLK_RALT, "Alt"},
        {SDLK_F1, "F1"}, {SDLK_F2, "F2"}, {SDLK_F3, "F3"}, {SDLK_F4, "F4"},
        {SDLK_F5, "F5"}, {SDLK_F6, "F6"}, {SDLK_F7, "F7"}, {SDLK_F8, "F8"},
        {SDLK_F9, "F9"}, {SDLK_F10, "F10"}, {SDLK_F11, "F11"}, {SDLK_F12, "F12"},
        {SDLK_LGUI, "Meta"}, {SDLK_RGUI, "Meta"},
    };
    for (auto& m : map)
        if (m.k == ks->sym) { return std::string("{\"k\":\"") + m.n + "\"}"; }
    Uint32 c = ks->sym;
    if (c >= 0x20 && c != 0x7F && c < 0x100) {
        *isChar = true;
        return std::string("{\"u\":") + std::to_string((int)c) + "}";
    }
    return "";
}

// ---------------- app SDL ----------------
// Ponto de entrada chamado pelo SDLActivity via dlopen/dlsym("SDL_main").
extern "C" __attribute__((visibility("default")))
int SDL_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    const char* dir = SDL_AndroidGetInternalStoragePath();
    if (!dir) {
        LOGE("desktop: sem diretório interno");
        return 1;
    }
    g_cfg = loadConfig(dir);
    if (g_cfg.envId.empty()) {
        LOGE("desktop: config ausente");
        return 1;
    }
    LOGI("desktop iniciando env=%s res=%dx%d", g_cfg.envId.c_str(), g_cfg.gw, g_cfg.gh);

    GlobalState::instance().setOpt("affinity", std::to_string(g_cfg.affinity));
    GlobalState::instance().setOpt("dynarec", std::to_string(g_cfg.dynarec));
    GlobalState::instance().setOpt("wineDebug", g_cfg.wineDebug);

    Bridge* b = BridgeManager::instance().bridge(g_cfg.envId);
    std::string hello = b->connect(g_cfg.envId);
    JsonValue hj = jsonParse(hello);
    std::string errMsg;
    if (!hj.getBool("ok")) {
        errMsg = hj.getString("error", "bridge indisponível");
    } else {
        g_cfg.gw = hj.getInt("w", g_cfg.gw);
        g_cfg.gh = hj.getInt("h", g_cfg.gh);
    }
    int gw = g_cfg.gw, gh = g_cfg.gh;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        LOGE("SDL_Init falhou: %s", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow(
        "NovaWin - Ambiente Windows",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 768,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN);
    if (!win) {
        LOGE("SDL_CreateWindow: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    int flags = g_cfg.accelerated ? SDL_RENDERER_ACCELERATED : SDL_RENDERER_SOFTWARE;
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, (SDL_RendererFlags)flags);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        LOGE("SDL_CreateRenderer: %s", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* frameTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
                                              SDL_TEXTUREACCESS_STREAMING, gw, gh);
    void* shmPtr = nullptr;
    int shmFd = -1;

    TTF_Font* font = nullptr;
    TTF_Font* fontBig = nullptr;
    bool hasText = false;
    if (errMsg.empty()) {
        if (TTF_Init() == 0) {
            std::string fontPath =
                GlobalState::instance().rootfsDir +
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
            font = TTF_OpenFont(fontPath.c_str(), 15);
            fontBig = TTF_OpenFont(fontPath.c_str(), 22);
            hasText = font != nullptr;
        }
    }
    auto renderText = [&](SDL_Renderer* r, TTF_Font* f, const std::string& text,
                          float x, float y, SDL_Color c) -> int {
        if (!f) return 0;
        SDL_Surface* s = TTF_RenderText_Blended(f, text.c_str(),
            (SDL_Color){c.r, c.g, c.b, (Uint8)255});
        if (!s) return 0;
        SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
        SDL_Rect dst{(int)x, (int)y, (int)s->w, (int)s->h};
        SDL_RenderCopy(r, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_FreeSurface(s);
        return (int)s->w;
    };

    auto mapShm = [&]() {
        if (shmFd >= 0) {
            munmap(shmPtr, (size_t)gw * gh * 4);
            close(shmFd);
            shmFd = -1;
            shmPtr = nullptr;
        }
        if (b->shmName.empty()) return;
        std::string path = "/dev/shm/" + b->shmName;
        shmFd = open(path.c_str(), O_RDONLY);
        if (shmFd < 0) {
            LOGE("shm nao abre: %s", path.c_str());
            return;
        }
        shmPtr = mmap(nullptr, (size_t)gw * gh * 4, PROT_READ, MAP_SHARED, shmFd, 0);
        if (shmPtr == MAP_FAILED) {
            LOGE("mmap shm falhou");
            close(shmFd);
            shmFd = -1;
            shmPtr = nullptr;
        }
    };
    mapShm();

    // estado UI
    bool showTaskMgr = false;
    bool imeOn = false;
    bool running = true;
    uint32_t lastFrameSeq = 0;
    float tapX = 0, tapY = 0;
    Uint32 tapStart = 0;
    std::vector<Rect> chipRects;     // botões de janela na taskbar
    std::vector<int> chipWinIds;
    std::vector<Rect> procRowRects;  // linhas do gerenciador
    std::vector<int> procRowPids;
    Rect killEnvRect;
    Rect backRect, kbRect, tmRect, powerRect;

    auto guestCoords = [&](int W, int H, float mx, float my, int& gx, int& gy) {
        float ty = H - TB_H;
        float scale = std::min(W / (float)gw, ty / (float)gh);
        float ox = (W - gw * scale) / 2;
        float oy = (ty - gh * scale) / 2;
        gx = (int)((mx - ox) / scale * g_cfg.pointerSpeed);
        gy = (int)((my - oy) / scale * g_cfg.pointerSpeed);
        gx = std::max(0, std::min(gw - 1, gx));
        gy = std::max(0, std::min(gh - 1, gy));
    };

    while (running) {
        SDL_WaitEventTimeout(nullptr, 33);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                case SDL_KEYUP: {
                    if (!g_cfg.hardwareKeyboard) break;
                    bool isChar = false;
                    std::string k = sdlKeyToBridgeKey(&e.key.keysym, &isChar);
                    if (!k.empty()) b->key(k, e.type == SDL_KEYDOWN);
                    break;
                }
                case SDL_TEXTINPUT:
                    if (imeOn) b->ime(e.text.text);
                    break;
                case SDL_FINGERDOWN:
                case SDL_FINGERUP:
                case SDL_FINGERMOTION: {
                    int W, H;
                    SDL_GetWindowSize(win, &W, &H);
                    float mx = e.tfinger.x * W;
                    float my = e.tfinger.y * H;
                    bool down = (e.type == SDL_FINGERDOWN);
                    bool inBar = my >= H - TB_H;

                    if (inBar) {
                        if (down) {
                            if (rectHit(backRect, mx, my)) {
                                running = false; // sessão continua viva
                                break;
                            }
                            for (size_t i = 0; i < chipRects.size(); i++)
                                if (rectHit(chipRects[i], mx, my)) {
                                    b->focus(chipWinIds[i]);
                                    break;
                                }
                            if (rectHit(kbRect, mx, my)) {
                                imeOn = !imeOn;
                                if (imeOn) SDL_StartTextInput();
                                else SDL_StopTextInput();
                            }
                            if (rectHit(tmRect, mx, my)) showTaskMgr = !showTaskMgr;
                            if (rectHit(powerRect, mx, my)) running = false;
                        }
                        break;
                    }
                    if (showTaskMgr && down) {
                        bool hit = false;
                        if (rectHit(killEnvRect, mx, my)) {
                            Launcher::instance().killSession(g_cfg.envId);
                            running = false;
                            hit = true;
                        }
                        for (size_t i = 0; i < procRowRects.size(); i++)
                            if (rectHit(procRowRects[i], mx, my)) {
                                b->killPid(procRowPids[i]);
                                hit = true;
                                break;
                            }
                        if (!hit) showTaskMgr = false; // toca fora: fecha
                        break;
                    }
                    // guest
                    int gx, gy;
                    guestCoords(W, H, mx, my, gx, gy);
                    if (down) {
                        b->pointer(gx, gy, 1);
                        tapX = mx; tapY = my; tapStart = SDL_GetTicks();
                    } else {
                        b->pointer(gx, gy, 0);
                    }
                    break;
                }
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP: {
                    int W, H;
                    SDL_GetWindowSize(win, &W, &H);
                    if (e.button.y >= H - TB_H) break; // barra tratada por touch
                    if (e.button.button == SDL_BUTTON_LEFT) break; // via touch/motion
                    int gx, gy;
                    guestCoords(W, H, e.button.x, e.button.y, gx, gy);
                    int bx = 0;
                    if (e.button.button == SDL_BUTTON_RIGHT) bx = 4;   // bit2 -> X button 3
                    else if (e.button.button == SDL_BUTTON_MIDDLE) bx = 2; // bit1 -> X button 2
                    b->pointer(gx, gy, e.button.clicks ? bx : 0);
                    break;
                }
                case SDL_MOUSEMOTION: {
                    int W, H;
                    SDL_GetWindowSize(win, &W, &H);
                    if (e.motion.y >= H - TB_H) break;
                    int gx, gy;
                    guestCoords(W, H, e.motion.x, e.motion.y, gx, gy);
                    b->pointer(gx, gy, (e.motion.state & SDL_BUTTON_LMASK) ? 1 : 0);
                    break;
                }
            }
        }

        b->pump();

        int W, H;
        SDL_GetWindowSize(win, &W, &H);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 6, 9, 16, 255);
        SDL_RenderClear(ren);

        if (!errMsg.empty()) {
            renderText(ren, fontBig, "Nao foi possivel conectar ao ambiente Windows.",
                       40, 120, {232, 236, 245, 255});
            renderText(ren, font, errMsg, 40, 170, {154, 165, 189, 255});
            renderText(ren, font, "Volte e tente novamente, ou veja o log em Configuracoes.",
                       40, 200, {154, 165, 189, 255});
        } else {
            // frame do guest (letterbox)
            float ty = H - TB_H;
            float scale = std::min(W / (float)gw, ty / (float)gh);
            float ox = (W - gw * scale) / 2;
            float oy = (ty - gh * scale) / 2;
            if (frameTex && shmPtr && b->frameSeq != lastFrameSeq) {
                SDL_UpdateTexture(frameTex, nullptr, shmPtr, gw * 4);
                lastFrameSeq = b->frameSeq;
            }
            if (frameTex) {
                SDL_Rect dst{(int)ox, (int)oy, (int)(gw * scale), (int)(gh * scale)};
                SDL_RenderCopy(ren, frameTex, nullptr, &dst);
            }
            if (g_cfg.showTouch && tapStart != 0) {
                int age = (int)(SDL_GetTicks() - tapStart);
                if (age < 350) {
                    float r = 12 + age * 0.05f;
                    SDL_SetRenderDrawColor(ren, 59, 130, 246, (Uint8)std::max(0, 120 - age / 3));
                    SDL_Rect rr{(int)(tapX - r), (int)(tapY - r), (int)(2 * r), (int)(2 * r)};
                    SDL_RenderDrawRect(ren, &rr);
                } else {
                    tapStart = 0;
                }
            }

            // ---------------- taskbar ----------------
            float tyf = (float)(H - TB_H);
            drawRoundedRect(ren, 0, tyf, W, TB_H + 20, 0, {21, 27, 43, 255});
            SDL_SetRenderDrawColor(ren, 42, 53, 80, 255);
            SDL_RenderDrawLine(ren, 0, (int)tyf, W, (int)tyf);

            Glyphs::back(ren, 12, tyf + 10, 28);
            backRect = {4, tyf + 2, 44, TB_H - 4};

            chipRects.clear();
            chipWinIds.clear();
            float wx = 56;
            for (size_t i = 0; i < b->windows.size() && i < 6; i++) {
                const JsonValue& wj = b->windows[i];
                std::string title = wj.getString("title");
                if (title.size() > 14) title = title.substr(0, 13) + "...";
                bool active = wj.getBool("active");
                Rect chip{wx, tyf + 8, 112, 32};
                drawRoundedRect(ren, chip.x, chip.y, chip.w, chip.h, 10,
                              active ? (SDL_Color){59, 130, 246, 255} : (SDL_Color){27, 35, 56, 255});
                renderText(ren, font, title.empty() ? "janela" : title, wx + 8, tyf + 15,
                           active ? (SDL_Color){255, 255, 255, 255} : (SDL_Color){232, 236, 245, 255});
                chipRects.push_back(chip);
                chipWinIds.push_back(wj.getInt("id"));
                wx += 120;
                if (wx > W - 170) break;
            }
            float rx = W - 150;
            Glyphs::keyboard(ren, rx, tyf + 10, 28);
            kbRect = {rx - 6, tyf + 2, 40, TB_H - 4};
            rx += 42;
            Glyphs::list(ren, rx, tyf + 10, 28);
            tmRect = {rx - 6, tyf + 2, 40, TB_H - 4};
            rx += 42;
            Glyphs::power(ren, rx, tyf + 10, 28);
            powerRect = {rx - 6, tyf + 2, 40, TB_H - 4};
            if (imeOn) {
                SDL_SetRenderDrawColor(ren, 59, 130, 246, 255);
                SDL_Rect _rc{(int)(rx - 24), (int)tyf + TB_H - 5, 60, 3}; SDL_RenderFillRect(ren, &_rc);;
            }

            // ---------------- gerenciador de tarefas ----------------
            if (showTaskMgr) {
                float pw = W * 0.88f, ph = H * 0.55f;
                float px = (W - pw) / 2, py = (H - ph) / 2;
                SDL_SetRenderDrawColor(ren, 0, 0, 0, 140);
                SDL_Rect _rc{0, 0, W, H}; SDL_RenderFillRect(ren, &_rc);;
                drawRoundedRect(ren, px, py, pw, ph, 18, {21, 27, 43, 255});
                renderText(ren, fontBig, "Gerenciador de processos", px + 18, py + 14,
                           {232, 236, 245, 255});
                renderText(ren, font, "Toque em um processo para encerra-lo.", px + 18, py + 44,
                           {154, 165, 189, 255});
                procRowRects.clear();
                procRowPids.clear();
                float ly = py + 70;
                bool empty = true;
                for (auto& p : b->procs) {
                    std::string name = p.getString("name");
                    if (name.empty()) continue;
                    empty = false;
                    drawRoundedRect(ren, px + 14, ly, pw - 28, 34, 8, {26, 34, 52, 255});
                    renderText(ren, font, name, px + 24, ly + 9, {232, 236, 245, 255});
                    renderText(ren, font, "PID " + std::to_string(p.getInt("pid")),
                               px + pw - 100, ly + 9, {154, 165, 189, 255});
                    procRowRects.push_back({px + 14, ly, pw - 28, 34});
                    procRowPids.push_back(p.getInt("pid"));
                    ly += 40;
                    if (ly > py + ph - 64) break;
                }
                if (empty)
                    renderText(ren, font, "nenhum processo guest ativo", px + 24, ly,
                               {154, 165, 189, 255});
                drawRoundedRect(ren, px + 14, py + ph - 50, pw - 28, 38, 10, {255, 69, 58, 255});
                renderText(ren, font, "Encerrar ambiente", px + 24, py + ph - 40,
                           {255, 255, 255, 255});
                killEnvRect = {px + 14, py + ph - 50, pw - 28, 38};
            }
        }
        SDL_RenderPresent(ren);
    }

    // encerra a sessão guest (evita processos órfãos de Xvfb/wine em 2º plano)
    if (errMsg.empty()) Launcher::instance().killSession(g_cfg.envId);
    BridgeManager::instance().release(g_cfg.envId);
    if (font) TTF_CloseFont(font);
    if (fontBig) TTF_CloseFont(fontBig);
    if (hasText) TTF_Quit();
    if (shmFd >= 0) {
        if (shmPtr) munmap(shmPtr, (size_t)gw * gh * 4);
        close(shmFd);
    }
    if (frameTex) SDL_DestroyTexture(frameTex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
    LOGI("desktop finalizado");
    return 0;
}
