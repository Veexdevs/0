package com.novawin.core

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

/**
 * Ponte JNI para o núcleo nativo (libnovawin.so).
 *
 * O núcleo nativo é quem:
 *  - extrai o rootfs guest (Linux x86-64 com Wine, Xvfb, openbox e bridge);
 *  - cria e mantém prefixos Wine por ambiente;
 *  - inicia sessões guest via Box64/Box86 (Xvfb + openbox + novawin-bridge + wine);
 *  - fala com o bridge guest (frames via MIT-SHM, entrada, janelas, processos);
 *  - executa operações de filesystem e controle de processos.
 */
object Runtime {

    const val TAG = "NovaWin.Runtime"

    fun interface ProgressCb {
        fun onProgress(done: Long, total: Long)
    }

    fun interface LineCb {
        fun onLine(line: String)
    }

    private var inited = false

    fun ensureInit(context: Context) {
        if (inited) return
        System.loadLibrary("novawin")
        nativeInit(context.filesDir.absolutePath)
        inited = true
    }

    // ---------------- Preparação do runtime ----------------

    fun prepareState(): Int = nativePrepareState()

    /** Extrai o rootfs e os emuladores dos assets. Bloqueante — chamar em thread. */
    fun prepare(context: Context, progress: ProgressCb): Int =
        nativePrepare(context.assets, progress)

    /** Versões reais dos componentes (JSON string). */
    fun info(): JSONObject {
        return try {
            JSONObject(nativeInfo())
        } catch (_: Exception) {
            JSONObject()
        }
    }

    // ---------------- Ambientes ----------------

    fun envList(): MutableList<Environment> {
        val list = mutableListOf<Environment>()
        try {
            val arr = JSONArray(nativeEnvsList())
            for (i in 0 until arr.length()) list.add(Environment.fromJson(arr.getJSONObject(i)))
        } catch (_: Exception) {
        }
        return list
    }

    /** Cria ambiente (registro + diretórios + prefixo vazio). Bloqueante. */
    fun envCreate(context: Context, env: Environment): Pair<Boolean, String> {
        val out = nativeEnvCreate(env.toJson().toString())
        return try {
            val o = JSONObject(out)
            Pair(o.optBoolean("ok"), o.optString("error"))
        } catch (_: Exception) {
            Pair(false, out)
        }
    }

    fun envDelete(envId: String): Int = nativeEnvDelete(envId)

    /** Inicializa o prefixo Wine (wineboot --init). Bloqueante — chamar em thread. */
    fun envPrefixInit(envId: String, log: LineCb): Int = nativeEnvPrefixInit(envId, log)

    // ---------------- Sessão / execução ----------------

    /**
     * Inicia (ou reutiliza) a sessão do ambiente e executa [target] (caminho do exe
     * dentro do rootfs) ou nenhum programa (apenas o desktop).
     */
    fun sessionStart(envId: String, target: String, opts: JSONObject): LaunchResult =
        parseLaunch(nativeSessionStart(envId, target, opts.toString()))

    fun sessionAlive(envId: String): Boolean = nativeSessionAlive(envId)

    fun sessionKill(envId: String): Int = nativeSessionKill(envId)

    fun launchInSession(envId: String, exePath: String): LaunchResult =
        parseLaunch(nativeLaunchInSession(envId, exePath))

    private fun parseLaunch(raw: String): LaunchResult {
        return try {
            val o = JSONObject(raw)
            LaunchResult(
                ok = o.optBoolean("ok"),
                message = o.optString("message"),
                detail = o.optString("detail"),
                pid = o.optInt("pid", -1)
            )
        } catch (_: Exception) {
            LaunchResult(ok = false, message = "Erro desconhecido no lançamento", detail = raw)
        }
    }

    // ---------------- Bridge (desktop guest) ----------------

    /** Conecta ao bridge do ambiente. JSON: {ok, shm, w, h} ou {ok:false, error}. */
    fun bridgeConnect(envId: String): JSONObject {
        return try {
            JSONObject(nativeBridgeConnect(envId))
        } catch (_: Exception) {
            JSONObject().put("ok", false).put("error", "bridge indisponível")
        }
    }

    fun bridgeDisconnect(envId: String): Int = nativeBridgeDisconnect(envId)

    fun bridgeFrameSeq(envId: String): Long = nativeBridgeFrameSeq(envId)
    fun bridgeShmName(envId: String): String = nativeBridgeShmName(envId)
    fun bridgeScreenSize(envId: String): IntArray =
        nativeBridgeScreenSize(envId).let { intArrayOf(it shr 16 and 0xFFFF, it and 0xFFFF) }

    fun bridgePointer(envId: String, x: Float, y: Float, buttons: Int): Int =
        nativeBridgePointer(envId, x, y, buttons)

    /** [key]: "U:<codepoint>" (caractere) ou "K:<nome>" (Enter, Space, Left, A..Z, F1..F12, Shift, Ctrl, Alt, Esc) */
    fun bridgeKey(envId: String, key: String, down: Boolean): Int =
        nativeBridgeKey(envId, key, down)

    /** Texto digitado via IME virtual (teclado do sistema). */
    fun bridgeIme(envId: String, text: String): Int = nativeBridgeIme(envId, text)

    fun bridgeWindows(envId: String): MutableList<GuestWindow> {
        val list = mutableListOf<GuestWindow>()
        try {
            val arr = JSONArray(nativeBridgeWindows(envId))
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                list.add(GuestWindow(o.optInt("id"), o.optString("title"),
                    o.optBoolean("active"), o.optBoolean("fullscreen")))
            }
        } catch (_: Exception) {
        }
        return list
    }

    fun bridgeFocus(envId: String, winId: Int): Int = nativeBridgeFocus(envId, winId)
    fun bridgeProcesses(envId: String): MutableList<ProcInfo> {
        val list = mutableListOf<ProcInfo>()
        try {
            val arr = JSONArray(nativeBridgeProcesses(envId))
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                list.add(ProcInfo(o.optInt("pid"), o.optString("name"), "R", true))
            }
        } catch (_: Exception) {
        }
        return list
    }

    fun bridgeKill(envId: String, pid: Int): Int = nativeBridgeKill(envId, pid)
    fun bridgeLog(envId: String, maxLines: Int): String = nativeBridgeLog(envId, maxLines)

    // ---------------- Filesystem ----------------

    fun fsList(path: String): MutableList<FileEntry> {
        val list = mutableListOf<FileEntry>()
        try {
            val arr = JSONArray(nativeFsList(path))
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                list.add(FileEntry(o.optString("name"), o.optString("path"),
                    o.optBoolean("dir"), o.optLong("size"), o.optLong("mtime")))
            }
        } catch (_: Exception) {
        }
        return list
    }

    fun fsMkdir(path: String): Int = nativeFsMkdir(path)
    fun fsDelete(path: String): Int = nativeFsDelete(path)
    fun fsCopy(src: String, dst: String): Int = nativeFsCopy(src, dst)
    fun fsMove(src: String, dst: String): Int = nativeFsMove(src, dst)
    fun fsInfo(path: String): FileEntry? = try {
        val o = JSONObject(nativeFsInfo(path))
        FileEntry(o.optString("name"), o.optString("path"),
            o.optBoolean("dir"), o.optLong("size"), o.optLong("mtime"))
    } catch (_: Exception) {
        null
    }

    // ---------------- Processos ----------------

    fun processes(): MutableList<ProcInfo> {
        val list = mutableListOf<ProcInfo>()
        try {
            val arr = JSONArray(nativeProcesses())
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                list.add(ProcInfo(o.optInt("pid"), o.optString("name"),
                    o.optString("state"), o.optBoolean("guest")))
            }
        } catch (_: Exception) {
        }
        return list
    }

    fun kill(pid: Int): Int = nativeKill(pid)
    fun setCpuAffinity(pid: Int, mask: Int): Int = nativeSetAffinity(pid, mask)

    // ---------------- nativos ----------------

    private external fun nativeInit(filesDir: String)
    private external fun nativePrepareState(): Int
    private external fun nativePrepare(assets: Any, cb: ProgressCb): Int
    private external fun nativeInfo(): String
    private external fun nativeEnvsList(): String
    private external fun nativeEnvCreate(json: String): String
    private external fun nativeEnvDelete(envId: String): Int
    private external fun nativeEnvPrefixInit(envId: String, log: LineCb): Int
    private external fun nativeSessionStart(envId: String, target: String, opts: String): String
    private external fun nativeSessionAlive(envId: String): Boolean
    private external fun nativeSessionKill(envId: String): Int
    private external fun nativeLaunchInSession(envId: String, exePath: String): String
    private external fun nativeBridgeConnect(envId: String): String
    private external fun nativeBridgeDisconnect(envId: String): Int
    private external fun nativeBridgeFrameSeq(envId: String): Long
    private external fun nativeBridgeShmName(envId: String): String
    private external fun nativeBridgeScreenSize(envId: String): Int
    private external fun nativeBridgePointer(envId: String, x: Float, y: Float, buttons: Int): Int
    private external fun nativeBridgeKey(envId: String, key: String, down: Boolean): Int
    private external fun nativeBridgeIme(envId: String, text: String): Int
    private external fun nativeBridgeWindows(envId: String): String
    private external fun nativeBridgeFocus(envId: String, winId: Int): Int
    private external fun nativeBridgeProcesses(envId: String): String
    private external fun nativeBridgeKill(envId: String, pid: Int): Int
    private external fun nativeBridgeLog(envId: String, maxLines: Int): String
    private external fun nativeFsList(path: String): String
    private external fun nativeFsMkdir(path: String): Int
    private external fun nativeFsDelete(path: String): Int
    private external fun nativeFsCopy(src: String, dst: String): Int
    private external fun nativeFsMove(src: String, dst: String): Int
    private external fun nativeFsInfo(path: String): String
    private external fun nativeProcesses(): String
    private external fun nativeKill(pid: Int): Int
    private external fun nativeSetAffinity(pid: Int, mask: Int): Int
}
