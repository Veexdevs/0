package com.novawin.desktop

import android.os.Bundle
import com.novawin.core.Store
import org.json.JSONObject
import org.libsdl.app.SDLActivity
import java.io.File

/**
 * Tela "Ambiente Windows".
 *
 * Estende a [SDLActivity] do SDL2: o SDL cria a janela em tela cheia e
 * executa o `android_main` do núcleo nativo (libnovawin.so), que:
 *  - conecta ao bridge do guest (socket unix);
 *  - exibe o frame do Xvfb (mit-shm) via SDL_Renderer (GLES2 ou software);
 *  - desenha a taskbar (janelas, teclado virtual, gerenciador de tarefas);
 *  - encaminha toque/mouse/teclado/IME para o guest (XTest).
 *
 * A configuração chega via arquivo (desktop_config.json) escrito ANTES de
 * super.onCreate, pois o SDL já inicializa o native na onCreate.
 */
class NovaWinDesktopActivity : SDLActivity() {

    /**
     * Bibliotecas carregadas pelo SDL (na ordem) e, por último, a que contém
     * o ponto de entrada `SDL_main` (libnovawin.so).
     */
    override fun getLibraries(): Array<String> = arrayOf("SDL2", "SDL2_ttf", "novawin")

    override fun onCreate(savedInstanceState: Bundle?) {
        // grava a config para o núcleo nativo ler (antes do SDL iniciar)
        try {
            val store = Store(this)
            val settings = store.loadSettings()
            val envId = intent.getStringExtra(EXTRA_ENV_ID) ?: ""
            val envs = store.loadEnvironments()
            val env = envs.find { it.id == envId }
            val cfg = JSONObject().apply {
                put("envId", envId)
                put("resolution", settings.optString("resolution", "1280x720"))
                put("renderer", settings.optString("renderer", "accelerated"))
                put("affinity", settings.optInt("affinity", -1))
                put("dynarec", settings.optInt("dynarec", 1))
                put("wineDebug", settings.optString("wineDebug", "-all"))
                put("pointerSpeed", (settings.optDouble("pointerSpeed", 1.0) * 100).toInt())
                put("showTouch", settings.optBoolean("showTouch", true))
                put("virtualKeyboard", settings.optBoolean("virtualKeyboard", true))
                put("hardwareKeyboard", settings.optBoolean("hardwareKeyboard", true))
                put("envName", env?.name ?: "")
            }
            File(filesDir, "desktop_config.json").writeText(cfg.toString())
        } catch (_: Exception) {
        }
        super.onCreate(savedInstanceState)
    }

    override fun onNewIntent(intent: android.content.Intent) {
        super.onNewIntent(intent)
    }

    companion object {
        const val EXTRA_ENV_ID = "env_id"
        const val EXTRA_RESOLUTION = "resolution"
        const val EXTRA_RENDERER = "renderer"
        const val EXTRA_AFFINITY = "affinity"
        const val EXTRA_DYNAREC = "dynarec"
        const val EXTRA_WINE_DEBUG = "wine_debug"
    }
}
