# NOTICE — Licenças e créditos

O NovaWin é distribuído sob **MIT** para o código original deste repositório.
Ele incorpora, referencia ou embute os seguintes softwares de terceiros,
cada um com sua própria licença:

## MIT
- **Box64** (https://github.com/ptitSeb/box64) — emulador x86-64→AArch64
  com recompilação dinâmica. Binário `box64` embutido nos assets.
- **Box86** (https://github.com/ptitSeb/box86) — emulador x86 (32 bits)→AArch64.
  Binário `box86` embutido nos assets.

## zlib License
- **SDL2** (https://www.libsdl.org) — biblioteca de interface gráfica usada
  pela tela de desktop. `libSDL2.so` embutida nos jniLibs.
- **SDL2_ttf** + **freetype** (libsdl.org / https://github.com/libsdl-org/SDL_ttf)
  — fontes TrueType no desktop. `libSDL2_ttf.so` embutida nos jniLibs.

## GNU GPL v2 / LGPL v2.1
- **Wine** (https://www.winehq.org) — implementação do Windows API que executa
  os programas .exe. Distribuído dentro do rootfs guest (pacotes WineHQ
  `wine-stable` / Debian `wine64`), como software independente: não é
  compilado nem vinculado ao app Android; o app apenas copia os pacotes e
  executa o binário `wine64`/`wine32` dentro do rootfs emulado.
- **openbox** (GPLv2) — gerenciador de janelas do guest.
- **Xvfb / xorg-server** (MIT/X11) — servidor X virtual do guest.

## Outros
- **Debian GNU/Linux 12 (bookworm)** — base do rootfs (debootstrap), incluindo
  glibc, utilidades e `novawin-bridge.c` compilado dentro do chroot na CI.
- Fontes **DejaVu** (Bitstream Vera license) e **Liberation** (SIL OFL).

O código original do NovaWin (app Kotlin, núcleo C++ em
`app/src/main/cpp/novawin`, bridge e scripts em `native/guest`, workflow de
CI) está licenciado sob MIT.
