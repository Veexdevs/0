/*
 * NovaWin Bridge (guest, x86-64 — roda sob Box64/Box86)
 *
 * Faz a ponte entre o desktop do guest (X11/Xvfb) e o app ARM64:
 *  - captura o frame raiz (XGetImage) e escreve RGBA8888 em SHM (MIT-SHM/
 *    /dev/shm compartilhado entre guest e host, mesma montagem do kernel);
 *  - envia eventos de frame, lista de janelas (_NET_CLIENT_LIST) e de
 *    processos via socket unix;
 *  - injeta entrada (XTest): ponteiro, teclado e IME.
 *
 * Build (na CI, dentro do chroot do rootfs):
 *   gcc -O2 -o /usr/local/bin/novawin-bridge novawin-bridge.c -lX11 -lXtst
 */
#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <time.h>

/* ---------------- protocolo (espelha xbridge.h do app) ---------------- */
typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  version;
    uint16_t flags;
    uint32_t length;
    uint32_t seq;
    uint32_t arg1;
    uint32_t arg2;
} nw_msg_t;

enum {
    GW_HELLO = 1, GW_FRAME = 2, GW_WINDOWS = 3, GW_PROCS = 4, GW_LOG = 5,
    GA_POINTER = 10, GA_KEY = 11, GA_IME = 12, GA_FOCUS = 13, GA_KILL = 14, GA_PING = 15
};

#define MAX_CLIENTS 1

static void logw(const char* fmt, ...) __attribute__((format(printf,1,2)));
static void logw(const char* fmt, ...) {
    va_list ap;
    __builtin_va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    __builtin_va_end(ap);
}

static int send_msg(int fd, uint8_t type, uint32_t arg1, const void* payload, uint32_t len) {
    nw_msg_t h;
    memset(&h, 0, sizeof(h));
    h.type = type;
    h.version = 1;
    h.length = len;
    h.arg1 = arg1;
    if (send(fd, &h, sizeof(h), MSG_NOSIGNAL) != (ssize_t)sizeof(h)) return -1;
    if (len > 0 && send(fd, payload, len, MSG_NOSIGNAL) != (ssize_t)len) return -1;
    return 0;
}

static int recv_msg(int fd, nw_msg_t* h, unsigned char* payload, uint32_t maxlen) {
    size_t got = 0;
    while (got < sizeof(nw_msg_t)) {
        ssize_t n = recv(fd, (char*)h + got, sizeof(nw_msg_t) - got, 0);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (n == 0) return -1;
        got += (size_t)n;
    }
    if (h->length > maxlen) return -2;
    uint32_t need = h->length;
    uint32_t off = 0;
    while (off < need) {
        ssize_t n = recv(fd, payload + off, need - off, 0);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (n == 0) return -1;
        off += (uint32_t)n;
    }
    return 0;
}

/* ---------------- estado ---------------- */
static int g_client = -1;
static int g_listen = -1;
static Display* g_dpy = NULL;
static int g_scr = 0;
static Window g_root = 0;
static int g_w = 0, g_h = 0;
static void* g_shm = NULL;
static int g_shm_fd = -1;
static char g_shm_name[128];
static uint32_t g_frame_seq = 0;
static uint32_t g_last_hash = 0;
static int g_last_buttons = 0;
static uint64_t g_last_win_ms = 0, g_last_proc_ms = 0, g_last_frame_ms = 0;
static char g_last_win_json[16384];
static char g_last_proc_json[16384];

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int g_running = 1;
static void on_term(int sig) { (void)sig; g_running = 0; }

/* ---------------- JSON simples (build string) ---------------- */
static void jesc(char* out, size_t cap, const char* in) {
    size_t o = 0;
    for (const unsigned char* p = (const unsigned char*)in; *p && o + 7 < cap; p++) {
        if (*p == '"' || *p == '\\') { out[o++] = '\\'; out[o++] = *p; }
        else if (*p < 0x20) { o += (size_t)snprintf(out + o, cap - o, "\\u%04x", *p); }
        else out[o++] = (char)*p;
    }
    out[o] = 0;
}

/* nomes usados pelo app -> nomes de keysym X11 */
static const char* xname(const char* n) {
    if (!strcmp(n, "Enter")) return "Return";
    if (!strcmp(n, "Page_Up")) return "Prior";
    if (!strcmp(n, "Page_Down")) return "Next";
    if (!strcmp(n, "Ctrl")) return "Control_L";
    if (!strcmp(n, "Alt")) return "Alt_L";
    if (!strcmp(n, "Shift")) return "Shift_L";
    if (!strcmp(n, "Meta")) return "Super_L";
    if (!strcmp(n, "Esc")) return "Escape";
    return n;
}

/* ---------------- wallpaper ---------------- */
static void draw_wallpaper(void) {
    int depth = DefaultDepth(g_dpy, g_scr);
    Pixmap pm = XCreatePixmap(g_dpy, g_root, g_w, g_h, depth);
    GC gc = XCreateGC(g_dpy, pm, 0, NULL);
    /* gradiente vertical: #0B0F1A -> #14213D */
    for (int y = 0; y < g_h; y++) {
        int t = y * 1000 / (g_h > 0 ? g_h : 1);
        int r = 11 + (20 - 11) * t / 1000;
        int g = 15 + (33 - 15) * t / 1000;
        int b = 26 + (61 - 26) * t / 1000;
        XSetForeground(g_dpy, gc, (r << 16) | (g << 8) | b);
        XDrawLine(g_dpy, pm, gc, 0, y, g_w, y);
    }
    /* "janela" decorativa central */
    int wx = g_w / 2 - 160, wy = g_h / 2 - 100;
    XSetForeground(g_dpy, gc, 0x2A3550);
    XFillRectangle(g_dpy, pm, gc, wx, wy, 320, 200);
    XSetForeground(g_dpy, gc, 0x3B82F6);
    XFillRectangle(g_dpy, pm, gc, wx, wy, 320, 28);
    XSetForeground(g_dpy, gc, 0x1B2338);
    for (int i = 1; i < 5; i++)
        XFillRectangle(g_dpy, pm, gc, wx + 12, wy + 44 + i * 28, 296 - (i % 2) * 60, 10);
    XCopyArea(g_dpy, pm, g_root, gc, 0, 0, g_w, g_h, 0, 0);
    XFreePixmap(g_dpy, pm);
    XFreeGC(g_dpy, gc);
    XSync(g_dpy, False);
}

/* ---------------- frame ---------------- */
static void push_frame(void) {
    if (!g_dpy || !g_shm) return;
    XImage* img = XGetImage(g_dpy, g_root, 0, 0, g_w, g_h, AllPlanes, ZPixmap);
    if (!img) return;
    /* hash amostral para pular frames idênticos */
    uint32_t hash = 2166136261u;
    int bps = img->bits_per_pixel == 32 ? 4 : (img->bytes_per_line * 4) / (img->width ? img->width : 1);
    for (int i = 0; i < 64; i++) {
        int px = (g_w * i) / 64;
        const unsigned char* row = img->data + (size_t)px * bps + ((size_t)(g_h * i) / 64) * img->bytes_per_line;
        hash ^= *(uint32_t*)row;
        hash *= 16777619u;
    }
    if (hash == g_last_hash) { XDestroyImage(img); return; }
    g_last_hash = hash;

    uint32_t red_mask = img->red_mask, green_mask = img->green_mask, blue_mask = img->blue_mask;
    int rsh = 16, gsh = 8, bsh = 0;
    if (red_mask)   rsh = (int)(31 - __builtin_clz(red_mask));
    if (green_mask) gsh = (int)(31 - __builtin_clz(green_mask));
    if (blue_mask)  bsh = (int)(31 - __builtin_clz(blue_mask));

    unsigned char* src = img->data;
    uint32_t* dst = (uint32_t*)g_shm;
    int n = g_w * g_h;
    if (img->bits_per_pixel == 32 && bps == 4) {
        const uint32_t* s32 = (const uint32_t*)src;
        for (int i = 0; i < n; i++) {
            uint32_t p = s32[i];
            uint32_t r = (p & red_mask) >> rsh;
            uint32_t g = (p & green_mask) >> gsh;
            if (!blue_mask) g = (p >> 8) & 0xFF;
            uint32_t b = blue_mask ? (p & blue_mask) >> bsh : (p & 0xFF);
            dst[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    } else {
        for (int y = 0; y < g_h; y++)
            for (int x = 0; x < g_w; x++) {
                const unsigned char* p = src + (size_t)y * img->bytes_per_line + (size_t)x * bps;
                uint32_t r, g, b;
                if (img->depth == 32 || (red_mask | green_mask | blue_mask) != 0) {
                    uint32_t v = 0;
                    for (int k = 0; k < bps; k++) v |= ((uint32_t)p[k]) << (8 * k);
                    r = (v & red_mask) >> rsh;
                    g = (v & green_mask) >> gsh;
                    b = blue_mask ? (v & blue_mask) >> bsh : v & 0xFF;
                } else {
                    r = p[0]; g = p[1]; b = p[2];
                }
                dst[y * g_w + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
    }
    XDestroyImage(img);
    g_frame_seq++;
    if (g_client >= 0) send_msg(g_client, GW_FRAME, g_frame_seq, NULL, 0);
}

/* ---------------- janelas ---------------- */
static void poll_windows(void) {
    if (!g_dpy) return;
    Atom net_list = XInternAtom(g_dpy, "_NET_CLIENT_LIST", False);
    Atom net_name = XInternAtom(g_dpy, "_NET_WM_NAME", False);
    Atom net_state = XInternAtom(g_dpy, "_NET_WM_STATE", False);
    Atom net_active = XInternAtom(g_dpy, "_NET_ACTIVE_WINDOW", False);

    unsigned long nlist = 0;
    char* buf = NULL;
    int rc = XGetWindowProperty(g_dpy, g_root, net_list, 0, 256, False, XA_WINDOW,
                                NULL, &nlist, (unsigned long*)&buf);
    Window active = None;
    unsigned long na = 0;
    char* abuf = NULL;
    if (XGetWindowProperty(g_dpy, g_root, net_active, 0, 1, False, XA_WINDOW,
                           NULL, &na, (unsigned long*)&abuf) == Success && na > 0 && abuf) {
        active = *(Window*)abuf;
        XFree(abuf);
    }

    char out[16384];
    int o = 0;
    o += snprintf(out + o, sizeof(out) - o, "[");
    if (rc == Success && nlist > 0 && buf) {
        Atom state_atom_fullscreen = XInternAtom(g_dpy, "_NET_WM_STATE_FULLSCREEN", False);
        for (unsigned long i = 0; i < nlist; i++) {
            Window w = ((Window*)buf)[i];
            char name[256] = "";
            Atom actual;
            int fmt;
            unsigned long n, after;
            unsigned char* pbuf = NULL;
            if (XGetWindowProperty(g_dpy, w, net_name, 0, 255, False, XA_UTF8_STRING,
                                   &actual, &fmt, &n, &after, &pbuf) == Success && n > 0 && pbuf) {
                memcpy(name, pbuf, n < 255 ? n : 255);
                name[n < 255 ? n : 255] = 0;
                XFree(pbuf);
            }
            int fs = 0;
            unsigned long ns = 0;
            unsigned char* sbuf = NULL;
            if (XGetWindowProperty(g_dpy, w, net_state, 0, 8, False, XA_ATOM,
                                   &actual, &fmt, &ns, &after, &sbuf) == Success && sbuf) {
                Atom* atoms = (Atom*)sbuf;
                for (unsigned long k = 0; k < ns; k++)
                    if (atoms[k] == state_atom_fullscreen) fs = 1;
                XFree(sbuf);
            }
            char esc[512];
            jesc(esc, sizeof(esc), name);
            o += snprintf(out + o, sizeof(out) - o,
                          "%s{\"id\":%lu,\"title\":\"%s\",\"active\":%d,\"fullscreen\":%d}",
                          i ? "," : "", (unsigned long)w, esc, w == active ? 1 : 0, fs);
        }
        XFree(buf);
    }
    o += snprintf(out + o, sizeof(out) - o, "]");

    if (strcmp(out, g_last_win_json) != 0) {
        strncpy(g_last_win_json, out, sizeof(g_last_win_json) - 1);
        if (g_client >= 0) send_msg(g_client, GW_WINDOWS, 0, out, (uint32_t)strlen(out));
    }
}

/* ---------------- processos ---------------- */
static int cmdline_is_guest(const char* cmd) {
    if (!cmd || !*cmd) return 0;
    return strstr(cmd, "wine") || strstr(cmd, ".exe") ||
           strstr(cmd, "novawin-session") || strstr(cmd, "Xvfb") ||
           strstr(cmd, "openbox") || strstr(cmd, "box64") || strstr(cmd, "box86");
}

static void poll_procs(void) {
    DIR* d = opendir("/proc");
    if (!d) return;
    char out[16384];
    int o = 0;
    o += snprintf(out + o, sizeof(out) - o, "[");
    int first = 1;
    struct dirent* ent;
    pid_t self = getpid();
    while ((ent = readdir(d)) != NULL) {
        char* endp = NULL;
        long pid = strtol(ent->d_name, &endp, 10);
        if (endp == ent->d_name || pid <= 0 || pid == self) continue;
        char path[64];
        snprintf(path, sizeof(path), "/proc/%ld/cmdline", pid);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        char cmd[512];
        ssize_t n = read(fd, cmd, sizeof(cmd) - 1);
        close(fd);
        if (n <= 0) continue;
        cmd[n] = 0;
        if (!cmdline_is_guest(cmd)) continue;
        char comm[64] = "";
        snprintf(path, sizeof(path), "/proc/%ld/comm", pid);
        fd = open(path, O_RDONLY);
        if (fd >= 0) {
            n = read(fd, comm, sizeof(comm) - 1);
            close(fd);
            if (n > 0) comm[n - 1] = 0;
        }
        char esc[256];
        jesc(esc, sizeof(esc), comm);
        o += snprintf(out + o, sizeof(out) - o,
                      "%s{\"pid\":%ld,\"name\":\"%s\"}", first ? "" : ",", pid, esc);
        first = 0;
    }
    closedir(d);
    o += snprintf(out + o, sizeof(out) - o, "]");
    if (strcmp(out, g_last_proc_json) != 0) {
        strncpy(g_last_proc_json, out, sizeof(g_last_proc_json) - 1);
        if (g_client >= 0) send_msg(g_client, GW_PROCS, 0, out, (uint32_t)strlen(out));
    }
}

/* ---------------- entrada ---------------- */
static void unicode_to_keysym(unsigned char* utf8, KeySym* ks) {
    *ks = XStringToKeysym((const char*)utf8);
}

static void inject_char(const unsigned char* utf8) {
    if (!g_dpy) return;
    KeySym ks;
    unicode_to_keysym((unsigned char*)utf8, &ks);
    KeyCode kc = XKeysymToKeycode(g_dpy, ks);
    if (kc == 0) return;
    XTestFakeKeyEvent(g_dpy, kc, True, CurrentTime);
    XTestFakeKeyEvent(g_dpy, kc, False, CurrentTime);
}

static void handle_pointer(float x, float y, int buttons) {
    if (!g_dpy) return;
    int xi = (int)x, yi = (int)y;
    if (xi < 0) xi = 0; if (yi < 0) yi = 0;
    if (xi >= g_w) xi = g_w - 1;
    if (yi >= g_h) yi = g_h - 1;
    XTestFakeMotionEvent(g_dpy, -1, xi, yi, CurrentTime);
    int prev = g_last_buttons;
    for (int bit = 0; bit < 3; bit++) {
        int btn = bit + 1;
        int now = (buttons >> bit) & 1;
        int was = (prev >> bit) & 1;
        if (now != was)
            XTestFakeButtonEvent(g_dpy, btn, now ? True : False, CurrentTime);
    }
    g_last_buttons = buttons;
    XSync(g_dpy, False);
}


/* ---------------- accept ---------------- */
static void accept_client(void) {
    if (g_client >= 0) return;
    g_client = accept(g_listen, NULL, NULL);
    if (g_client < 0) return;
    fcntl(g_client, F_SETFL, O_NONBLOCK);
    /* envia HELLO */
    char hello[256];
    int len = snprintf(hello, sizeof(hello),
                       "{\"shm\":\"%s\",\"w\":%d,\"h\":%d,\"wine32\":0,\"bridge\":\"1.0\"}",
                       g_shm_name, g_w, g_h);
    send_msg(g_client, GW_HELLO, 0, hello, (uint32_t)len);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);
    signal(SIGPIPE, SIG_IGN);

    const char* nw_tmp = getenv("NW_TMP");
    if (!nw_tmp) nw_tmp = "/tmp";
    char sock_dir[512];
    snprintf(sock_dir, sizeof(sock_dir), "%s/.novawin", nw_tmp);
    mkdir(sock_dir, 0755);
    char sock_path[512];
    snprintf(sock_path, sizeof(sock_path), "%s/bridge.sock", sock_dir);
    unlink(sock_path);

    g_listen = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_listen < 0) { logw("socket: %s\n", strerror(errno)); return 1; }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
    if (bind(g_listen, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        logw("bind: %s\n", strerror(errno));
        return 1;
    }
    listen(g_listen, 1);
    fcntl(g_listen, F_SETFL, O_NONBLOCK);

    /* abre display (Xvfb sobe em paralelo; tenta por até 60s) */
    for (int i = 0; i < 120 && g_running; i++) {
        g_dpy = XOpenDisplay(getenv("DISPLAY") ? getenv("DISPLAY") : "127.0.0.1:0");
        if (g_dpy) break;
        usleep(500 * 1000);
    }
    if (!g_dpy) { logw("nao consegui abrir o display X\n"); return 1; }
    g_scr = DefaultScreen(g_dpy);
    g_root = RootWindow(g_dpy, g_scr);
    g_w = DisplayWidth(g_dpy, g_scr);
    g_h = DisplayHeight(g_dpy, g_scr);
    XSelectInput(g_dpy, g_root, ExposureMask | StructureNotifyMask | SubstructureNotifyMask);

    /* SHM compartilhada (guest e host veem o mesmo /dev/shm) */
    snprintf(g_shm_name, sizeof(g_shm_name), "nw-frame-%d", (int)getpid());
    g_shm_fd = shm_open(g_shm_name, O_CREAT | O_RDWR, 0666);
    if (g_shm_fd < 0) { logw("shm_open: %s\n", strerror(errno)); return 1; }
    ftruncate(g_shm_fd, (off_t)g_w * g_h * 4);
    g_shm = mmap(NULL, (size_t)g_w * g_h * 4, PROT_READ | PROT_WRITE, MAP_SHARED, g_shm_fd, 0);
    if (g_shm == MAP_FAILED) { logw("mmap shm\n"); return 1; }

    draw_wallpaper();

    unsigned char payload[65536];
    uint64_t last = 0;
    while (g_running) {
        uint64_t t = now_ms();
        if (g_client < 0) accept_client();

        struct pollfd pfs[3];
        int nfd = 0;
        if (g_client >= 0) pfs[nfd++] = (struct pollfd){g_client, POLLIN, 0};
        pfs[nfd++] = (struct pollfd){g_listen, POLLIN, 0};
        pfs[nfd++] = (struct pollfd){ConnectionNumber(g_dpy), POLLIN, 0};
        int pr = poll(pfs, nfd, 30);
        if (pr > 0) {
            for (int i = 0; i < nfd; i++) {
                if (!(pfs[i].revents & (POLLIN | POLLERR | POLLHUP))) continue;
                if (i < (int)(g_client >= 0 ? 1 : 0)) { /* cliente */
                    nw_msg_t h;
                    int rc = recv_msg(g_client, &h, payload, sizeof(payload));
                    if (rc != 0) {
                        close(g_client);
                        g_client = -1;
                        continue;
                    }
                    switch (h.type) {
                        case GA_POINTER:
                            if (h.length >= 8) {
                                float x, y;
                                memcpy(&x, payload, 4);
                                memcpy(&y, payload + 4, 4);
                                handle_pointer(x, y, (int)h.arg1);
                            }
                            break;
                        case GA_KEY:
                            if (h.length > 0 && h.length < sizeof(payload)) {
                                payload[h.length] = 0;
                                KeySym ks;
                                if (strstr((const char*)payload, "\"u\"")) {
                                    int cp = 0;
                                    sscanf((const char*)payload, "{\"u\":%d", &cp);
                                    if (cp > 0) {
                                        char utf8[8];
                                        if (cp < 0x80) utf8[0] = (char)cp;
                                        else if (cp < 0x800) {
                                            utf8[0] = (char)(0xC0 | (cp >> 6));
                                            utf8[1] = (char)(0x80 | (cp & 0x3F));
                                        } else {
                                            utf8[0] = (char)(0xE0 | (cp >> 12));
                                            utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                            utf8[2] = (char)(0x80 | (cp & 0x3F));
                                        }
                                        utf8[3] = 0;
                                        ks = XStringToKeysym(utf8);
                                    } else ks = NoSymbol;
                                } else if (strstr((const char*)payload, "\"k\"")) {
                                    const char* p = strchr((const char*)payload, '"');
                                    KeySym k2 = NoSymbol;
                                    if (p) {
                                        p++;
                                        const char* q = strchr(p, '"');
                                        if (q) {
                                            char name[32];
                                            size_t n = (size_t)(q - p);
                                            if (n > 31) n = 31;
                                            memcpy(name, p, n);
                                            name[n] = 0;
                                            k2 = XStringToKeysym(xname(name));
                                        }
                                    }
                                    ks = k2;
                                } else ks = NoSymbol;
                                if (ks != NoSymbol) {
                                    KeyCode kc = XKeysymToKeycode(g_dpy, ks);
                                    if (kc) {
                                        XTestFakeKeyEvent(g_dpy, kc, h.arg1 ? True : False, CurrentTime);
                                        XSync(g_dpy, False);
                                    }
                                }
                            }
                            break;
                        case GA_IME:
                            if (h.length > 0 && h.length < sizeof(payload)) {
                                payload[h.length] = 0;
                                const unsigned char* s = payload;
                                while (*s) {
                                    unsigned char utf8[8];
                                    int n = 1;
                                    if ((*s & 0xE0) == 0xC0) n = 2;
                                    else if ((*s & 0xF0) == 0xE0) n = 3;
                                    else if ((*s & 0xF8) == 0xF0) n = 4;
                                    if (n > 4) n = 4;
                                    if ((size_t)n > h.length - (s - payload)) break;
                                    memcpy(utf8, s, n);
                                    utf8[n] = 0;
                                    inject_char(utf8);
                                    s += n;
                                }
                                XSync(g_dpy, False);
                            }
                            break;
                        case GA_FOCUS: {
                            Window w = (Window)h.arg1;
                            Atom net_active = XInternAtom(g_dpy, "_NET_ACTIVE_WINDOW", False);
                            long vals[2] = { (long)w, (long)CurrentTime };
                            XChangeProperty(g_dpy, g_root, net_active, XA_WINDOW, 32,
                                            PropModeReplace, (unsigned char*)vals, 2);
                            XMapRaised(g_dpy, w);
                            XSync(g_dpy, False);
                            break;
                        }
                        case GA_KILL:
                            if (h.arg1 > 1) kill((pid_t)h.arg1, SIGTERM);
                            break;
                        case GA_PING:
                            send_msg(g_client, GW_LOG, 0, "{\"t\":\"pong\"}", 13);
                            break;
                        default:
                            break;
                    }
                } else if (pfs[i].revents & POLLIN) {
                    /* eventos X: descarta (nada crítico) e mantém o buffer limpo */
                    XEvent ev;
                    while (XPending(g_dpy)) {
                        XNextEvent(g_dpy, &ev);
                    }
                }
            }
        }

        if (t - last >= 33) { /* ~30 fps */
            last = t;
            if (t - g_last_frame_ms >= 33) {
                g_last_frame_ms = t;
                push_frame();
            }
            if (t - g_last_win_ms >= 500) {
                g_last_win_ms = t;
                poll_windows();
            }
            if (t - g_last_proc_ms >= 1000) {
                g_last_proc_ms = t;
                poll_procs();
            }
        }
        usleep(4000);
    }

    if (g_shm) munmap(g_shm, (size_t)g_w * g_h * 4);
    if (g_shm_fd >= 0) {
        close(g_shm_fd);
        shm_unlink(g_shm_name);
    }
    if (g_dpy) XCloseDisplay(g_dpy);
    close(g_listen);
    return 0;
}
