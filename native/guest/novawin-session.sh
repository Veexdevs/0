#!/bin/sh
# NovaWin — supervisor de sessão guest (roda sob Box64/Box86)
# uso: novawin-session.sh <WxH> [target.exe ...]
#
# Sobe: Xvfb (:0, TCP 127.0.0.1:6000 + socket em $X11_SOCKET_DIR),
#       openbox (gerenciador de janelas) e novawin-bridge (frames/entrada).
# Depois executa o alvo com o Wine (wine64/wine32, via $NW_WINE).
# A sessão permanece viva (desktop ocioso) até o app encerrá-la (SIGTERM).

RES="$1"; shift
W="${RES%%x*}"
H="${RES#*x}"
[ -n "$W" ] || W=1280
[ -n "$H" ] || H=720

export DISPLAY="${DISPLAY:-127.0.0.1:0}"
NW_TMP="${NW_TMP:-/tmp}"
NW_WINE="${NW_WINE:-wine64}"
SOCKDIR="$NW_TMP/.novawin"
mkdir -p "$SOCKDIR"
rm -f "$SOCKDIR/bridge.sock"

# socket do display X no diretório do ambiente (Xvfb respeita X11_SOCKET_DIR)
XSD="${X11_SOCKET_DIR:-/tmp/.X11-unix}"
mkdir -p "$XSD"
rm -f "$XSD/X0"
export X11_SOCKET_DIR="$XSD"

Xvfb :0 -screen 0 "${W}x${H}x24" -noextension GLX -dpms off \
    >"$NW_TMP/xvfb.log" 2>&1 &
XPID=$!

openbox >"$NW_TMP/openbox.log" 2>&1 &
OPID=$!

novawin-bridge >"$NW_TMP/bridge.log" 2>&1 &
BPID=$!

# aguarda o bridge (ele só cria o socket após abrir o display)
i=0
while [ "$i" -lt 120 ]; do
    if [ -S "$SOCKDIR/bridge.sock" ]; then break; fi
    if ! kill -0 "$BPID" 2>/dev/null; then break; fi
    sleep 0.5
    i=$((i+1))
done

# executa o alvo com Wine (se houver)
if [ "$#" -gt 0 ]; then
    "$NW_WINE" "$@" >>"$NW_TMP/wine.log" 2>&1 &
    WPID=$!
    wait "$WPID"
    CODE=$?
    echo "[novawin] wine exited: $CODE" >>"$NW_TMP/wine.log"
fi

# mantém o ambiente vivo até ser encerrado pelo app
trap 'kill $XPID $OPID $BPID 2>/dev/null; exit 0' TERM INT
while :; do sleep 3600; done
