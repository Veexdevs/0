package com.novawin

import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Density
import com.novawin.core.Arch
import com.novawin.core.Environment
import com.novawin.core.LaunchResult
import com.novawin.core.ProgramInfo
import com.novawin.core.Runtime
import com.novawin.core.Store
import com.novawin.desktop.NovaWinDesktopActivity
import com.novawin.ui.screens.EnvironmentsScreen
import com.novawin.ui.screens.FileManagerScreen
import com.novawin.ui.screens.HomeScreen
import com.novawin.ui.screens.ImportScreen
import com.novawin.ui.screens.ProgramsScreen
import com.novawin.ui.screens.SettingsScreen
import com.novawin.ui.theme.NovaWinTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject

sealed class Screen(val title: String) {
    object Home : Screen("NovaWin")
    object Import : Screen("Executar EXE")
    object Programs : Screen("Meus Programas")
    object Files : Screen("Gerenciador de Arquivos")
    object Settings : Screen("Configurações")
    object Envs : Screen("Ambientes Windows")
}

class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: android.os.Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = 0xFF0B0F1A.toInt()
        window.navigationBarColor = 0xFF0B0F1A.toInt()
        setContent {
            val store = Store(this)
            NovaWinTheme {
                // escala da interface (configuracao real: multiplica a densidade)
                val scale = store.loadSettings().optDouble("uiScale", 1.0).coerceIn(0.8, 1.6)
                val baseDensity = LocalDensity.current
                CompositionLocalProvider(
                    LocalDensity provides Density(density = baseDensity.density * scale.toFloat())
                ) {
                    Surface(modifier = Modifier.fillMaxSize()) {
                        AppRoot(store = store)
                    }
                }
            }
        }
    }
}

@Composable
fun AppRoot(store: Store) {
    var stack by remember { mutableStateOf(mutableListOf<Screen>(Screen.Home)) }
    val current = stack.last()
    val back: () -> Unit = { if (stack.size > 1) stack.removeAt(stack.size - 1) }
    val nav: (Screen) -> Unit = { stack.add(it) }

    when (current) {
        Screen.Home -> HomeScreen(store, nav)
        Screen.Import -> ImportScreen(store, nav, back)
        Screen.Programs -> ProgramsScreen(store, nav, back)
        Screen.Files -> FileManagerScreen(store, nav, back)
        Screen.Settings -> SettingsScreen(store, nav, back)
        Screen.Envs -> EnvironmentsScreen(store, nav, back)
    }
}

/** Estado de preparação do runtime (rootfs + emuladores). */
object RuntimeState {
    const val PENDING = 0
    const val RUNNING = 1
    const val READY = 2
    const val ERROR = -1
}

/**
 * Lógica real de lançamento de programas Windows:
 *  1. runtime pronto (rootfs extraído);
 *  2. prefixo Wine do ambiente inicializado (wineboot --init);
 *  3. sessão guest iniciada (Box64/Box86 + Xvfb + openbox + bridge);
 *  4. wine executa o .exe; a janela aparece no desktop do app.
 */
object Launcher {

    fun defaultEnv(store: Store): Environment? = store.loadEnvironments().firstOrNull()

    /** Inicia o prefixo Wine caso ainda não esteja pronto (bloqueante — em background). */
    suspend fun ensurePrefix(
        env: Environment,
        store: Store,
        onLine: (String) -> Unit = {}
    ): Int = withContext(Dispatchers.IO) {
        if (env.prefixReady) return@withContext 0
        val rc = Runtime.envPrefixInit(env.id, Runtime.LineCb { line -> onLine(line) })
        if (rc == 0) {
            markPrefixReady(store, env.id)
        }
        rc
    }

    fun markPrefixReady(store: Store, envId: String) {
        val list = store.loadEnvironments().map {
            if (it.id == envId) it.copy(prefixReady = true) else it
        }
        store.saveEnvironments(list)
    }

    /**
     * Lança [exePath] (caminho absoluto dentro do rootfs) no [env];
     * com [exePath] nulo, abre apenas o desktop.
     * Devolve o resultado real (erros vêm do runtime nativo / wine).
     */
    suspend fun launch(
        context: Context,
        store: Store,
        env: Environment,
        exePath: String?,
        program: ProgramInfo? = null
    ): LaunchResult = withContext(Dispatchers.IO) {
        if (Runtime.prepareState() != RuntimeState.READY) {
            return@withContext LaunchResult(false, "O ambiente de execução ainda não está pronto.",
                "Conclua a preparação na tela inicial antes de executar programas.")
        }
        if (env.arch == Arch.WIN32 && !Runtime.info().optBoolean("wine32")) {
            return@withContext LaunchResult(false, "Este sistema não tem suporte a programas de 32 bits.",
                "O Wine 32 bits (wine32) não está disponível neste rootfs; use um ambiente de 64 bits.")
        }
        // 1) prefixo Wine
        val rc = try {
            ensurePrefix(env, store)
        } catch (e: Exception) {
            return@withContext LaunchResult(false, "Falha ao inicializar o prefixo Wine.", e.message ?: "")
        }
        if (rc != 0) {
            return@withContext LaunchResult(false, "Falha ao inicializar o prefixo Wine (código $rc).",
                "O prefixo do Wine não pôde ser criado neste dispositivo. Veja o log em Configurações > Avançado.")
        }
        // 2) sessão guest + programa
        val res = if (exePath == null) {
            Runtime.sessionStart(env.id, "", sessionOptions(context, store))
        } else {
            Runtime.sessionStart(env.id, exePath, sessionOptions(context, store))
        }
        if (res.ok) {
            program?.let {
                store.upsertProgram(it.copy(lastRunAt = System.currentTimeMillis()))
            }
        }
        res
    }

    /** Opções de sessão derivadas das configurações reais do app. */
    fun sessionOptions(context: Context, store: Store): JSONObject {
        val s = store.loadSettings()
        return JSONObject().apply {
            put("resolution", s.optString("resolution", "1280x720"))
            put("renderer", s.optString("renderer", "accelerated"))
            put("dynarec", s.optInt("dynarec", 1))
            put("wineDebug", s.optString("wineDebug", "-all"))
            put("affinity", s.optInt("affinity", -1)) // -1 = todos os núcleos
        }
    }

    /** Abre o desktop (Ambiente Windows) do ambiente. */
    fun openDesktop(context: Context, env: Environment) {
        val s = Store(context).loadSettings()
        val intent = Intent(context, NovaWinDesktopActivity::class.java).apply {
            putExtra(NovaWinDesktopActivity.EXTRA_ENV_ID, env.id)
            putExtra(NovaWinDesktopActivity.EXTRA_RESOLUTION, s.optString("resolution", "1280x720"))
            putExtra(NovaWinDesktopActivity.EXTRA_RENDERER, s.optString("renderer", "accelerated"))
            putExtra(NovaWinDesktopActivity.EXTRA_AFFINITY, s.optInt("affinity", -1))
            putExtra(NovaWinDesktopActivity.EXTRA_DYNAREC, s.optInt("dynarec", 1))
            putExtra(NovaWinDesktopActivity.EXTRA_WINE_DEBUG, s.optString("wineDebug", "-all"))
        }
        context.startActivity(intent)
    }

    fun deviceRamMb(): Int {
        return try {
            val am = NovaWinApp.ctx()
                .getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager ?: return 0
            val info = ActivityManager.MemoryInfo()
            am.getMemoryInfo(info)
            (info.totalMem / (1024 * 1024)).toInt()
        } catch (_: Exception) {
            0
        }
    }
}
