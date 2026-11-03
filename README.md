# NovaWin

**Windows de verdade no Android** — execute programas `.exe` com **Wine** rodando
em um rootfs Debian x86-64 emulado por **Box64/Box86** (recompilação dinâmica a
AArch64), com interface própria estilo Windows 11 (dark, cantos arredondados,
azul), construída em Kotlin/Compose + núcleo nativo C++/SDL2.

```
┌────────────────────────────────────────────────────────────┐
│  App Android (Kotlin/Compose + libnovawin.so / SDL2)       │
│  - importação de .exe (SAF)                               │
│  - ambientes (prefixos Wine independentes)                 │
│  - gerenciador de arquivos / configurações reais           │
│  - desktop: frames do guest via SHM + taskbar + entrada    │
└──────────────────────────┬─────────────────────────────────┘
                           │ socket unix + /dev/shm (mesmo uid)
┌──────────────────────────▼─────────────────────────────────┐
│  Box64/Box86 (AArch64 nativo) → rootfs Debian 12 x86-64    │
│  - Wine (wine-stable)                                      │
│  - Xvfb + openbox (janelas reais)                          │
│  - novawin-bridge: captura frames (XGetImage → SHM RGBA)   │
│    e injeta entrada (XTest: teclado, mouse, IME)           │
│  - novawin-session.sh: supervisor da sessão                │
└────────────────────────────────────────────────────────────┘
```

Nada é simulado: cada processo do Windows roda de fato no Wine, o desktop que
você vê é o framebuffer do Xvfb do guest, e o toque/teclado chegam ao Windows
via XTest.

## Recursos

- **Tela inicial** com logo, atalhos e programas recentes.
- **Executar EXE**: escolha do .exe via SAF (permissão oficial do Android),
  verificação real do cabeçalho PE (32/64 bits), importação para o ambiente e
  lançamento com mensagens de erro reais (PE inválido, prefixo, wineboot,
  saída do processo, tempo limite).
- **Meus Programas**: lista dos executáveis importados por ambiente.
- **Gerenciador de Arquivos**: navega as pastas do ambiente (`C:\` =
  `prefix/drive_c`, apps, diretório do ambiente), criar pasta, apagar,
  copiar/mover, importar de fora (SAF).
- **Ambiente Windows**:
  - desktop com janelas independentes (openbox), barra de tarefas com chips de
    janela (toque = foco), resolução configurável;
  - teclado virtual (IME do Android → caracteres UTF-8 no guest), teclado
    físico e mouse (botões direito/médio);
  - gerenciador de tarefas do guest (listar processos, matar PID, encerrar
    ambiente).
- **Ambientes**: múltiplos prefixos Wine independentes (nome, arquitetura
  64/32 bits), cada um com seu diretório próprio.
- **Configurações**: apenas o que está de fato implementado — resolução,
  renderer (acelerado/software), memória (exibida), afinidade de CPU,
  dynarec (Box64), depuração do Wine, velocidade do ponteiro, controles de
  toque, teclado virtual/físico.

## Arquitetura e decisões

- **Sem root, sem chroot no Android**: o rootfs é um diretório normal no
  armazenamento privado do app (`filesDir/rootfs`), e o Box64 emula o binário
  x86-64 como processo filho do app — o `/dev/shm` e os sockets unix do
  ambiente são compartilhados naturalmente (mesmo uid).
- **Pacote**: o `rootfs.tar.gz` (~120–150 MB) e os emuladores `box64`/`box86`
  são **assets do APK**, produzidos pela CI. Na primeira execução, o app
  extrai o rootfs (barra de progresso real).
- **Bridge**: protocolo binário próprio (cabeçalho 24 bytes) sobre socket
  unix; frames em memória compartilhada `nw-frame-<pid>` (RGBA8888) com
  skipping por hash quando a tela não muda (~30 fps).
- **Sessão**: `box64 novawin-session.sh WxH [alvo.exe]` sobe Xvfb
  (`-noextension GLX` → Wine GDI/softpipe), openbox e o bridge; permanece
  viva (desktop ocioso) até ser encerrada pelo app (SIGTERM → trap mata os
  filhos).

## Compilando localmente (opcional)

O caminho recomendado é a CI (ela produz o APK com o rootfs embutido). Para
apenas o app Android (sem o rootfs nos assets):

```
./gradlew assembleDebug
```

Requer JDK 17, Android SDK (platform 35, build-tools 35.0.0) e
NDK `27.1.12297006`. O núcleo C++ usa SDL2/SDL2_ttf fornecidos em
`app/src/main/cpp/third_party/sdl2` (a CI baixa/constrói e injeta).

## CI (GitHub Actions)

`.github/workflows/android-build.yml` — dispara em `push` para `main`,
manualmente (Actions → NovaWin Build) e em tags `v*` (publica GitHub Release):

| Job      | O que faz                                                            |
| -------- | -------------------------------------------------------------------- |
| `runtimes` | Compila Box64 v0.4.4, Box86 v0.3.8, SDL2 2.30.9 e SDL2_ttf (freetype embutido) para `arm64-v8a` com NDK r27c |
| `rootfs`   | `debootstrap` Debian 12 (amd64) + Wine (WineHQ wine-stable, fallback Debian) + Xvfb/openbox/fontes + `novawin-bridge` compilado dentro do chroot; empacota `rootfs.tar.gz` |
| `app`      | JDK 17 + Android SDK + NDK; injeta assets (rootfs + emuladores + SDL2) e `./gradlew assembleDebug` → **`NovaWin.apk`** como artefato |
| `release`  | Em tag `v*`: publica `NovaWin.apk` + SHA-256 no GitHub Release        |

Versões fixadas em `env:` no topo do workflow (reprodutível).

## Instalação e uso

1. Baixe `NovaWin.apk` (artefato da build ou GitHub Release).
2. No Android (10+), habilite instalação de fontes desconhecidas e instale.
3. Abra o app → **Preparar ambiente** na tela inicial (extrai o rootfs;
   requer ~600 MB livres).
4. **Executar EXE** → escolha um `.exe` → configure o ambiente (nome,
   arquitetura, resolução) → **Iniciar**. Na primeira inicialização o Wine
   cria o prefixo (wineboot) — pode levar alguns minutos.
5. O desktop abre com a taskbar: toque num chip para focar a janela, no
   ícone de teclado para o IME, na lista para o gerenciador de tarefas e no
   botão de energia para encerrar o ambiente.

> Desempenho: Wine x86-64 em emulação (Box64) é lento; programas leves
> (Notepad++, calculadora, jogos simples, etc.) funcionam melhor. O modo
> 32 bits usa Box86 + wine32 quando disponível no rootfs.

## Licenças

- Código do NovaWin: **MIT** (veja `NOTICE.md`).
- Box64/Box86: **MIT** (ptitSeb).
- SDL2 / SDL2_ttf / freetype: **zlib** (libsdl.org).
- Wine: **GPLv2 / LGPLv2.1** (WineHQ) — roda como software independente
  dentro do rootfs, sem vinculação com o app.
- Debian/openbox/Xvfb/Xorg: licenças próprias (ver `debian/copyright` do
  rootfs).

## Estrutura

```
app/
  src/main/java/com/novawin/       app Kotlin (telas, store, runtime)
  src/main/java/org/libsdl/app/    SDLActivity e glue do SDL2 (zlib)
  src/main/cpp/novawin/            núcleo C++ (launcher, bridge, desktop)
  src/main/cpp/CMakeLists.txt      build NDK do núcleo
native/guest/
  novawin-bridge.c                 bridge X11↔app (compilado na CI no chroot)
  novawin-session.sh               supervisor da sessão guest
.github/workflows/android-build.yml  CI completa
```
