package com.novawin.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.novawin.Screen
import com.novawin.core.Runtime
import com.novawin.core.Store
import androidx.compose.foundation.clickable
import com.novawin.ui.components.NwButton
import com.novawin.ui.components.NwCard
import com.novawin.ui.components.NwDialog
import com.novawin.ui.components.SectionLabel
import com.novawin.ui.components.SettingRow
import com.novawin.ui.components.TopBar
import com.novawin.ui.theme.NwOk
import com.novawin.ui.theme.NwTextPrimary
import com.novawin.ui.theme.NwTextSecondary
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import org.json.JSONObject

@Composable
fun SettingsScreen(store: Store, nav: (Screen) -> Unit, back: () -> Unit) {
    var settings by remember { mutableStateOf(store.loadSettings()) }
    val scope = rememberCoroutineScope()

    fun update(key: String, value: Any) {
        val next = JSONObject(settings.toString()).put(key, value)
        settings = next
        store.saveSettings(next)
    }

    var dialog by remember { mutableStateOf<String?>(null) }
    var logText by remember { mutableStateOf("") }

    val cores = Runtime.info().optInt("cores", 8)

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 20.dp)
    ) {
        TopBar("Configurações", onBack = back)

        SectionLabel("Vídeo e desempenho")
        SettingRow(
            "Resolução do ambiente",
            settings.optString("resolution", "1280x720"),
            onClick = { dialog = "resolution" }
        )
        Spacer(Modifier.height(8.dp))
        SettingRow(
            "Renderizador gráfico (SDL2)",
            if (settings.optString("renderer", "accelerated") == "software") "Software" else "GLES2 (acelerado)",
            onClick = { dialog = "renderer" }
        )
        Spacer(Modifier.height(8.dp))
        SettingRow(
            "Núcleos de CPU",
            describeAffinity(settings.optInt("affinity", -1), cores),
            onClick = { dialog = "affinity" }
        )
        Spacer(Modifier.height(8.dp))
        SettingRow(
            "Emulação Box64 — DynaRec",
            if (settings.optInt("dynarec", 1) == 1) "Ativada (mais rápida)" else "Desativada (mais compatível)",
            onClick = { dialog = "dynarec" }
        )

        SectionLabel("Interface")
        SettingRow(
            "Escala da interface",
            "${settings.optDouble("uiScale", 1.0)}x",
            onClick = { dialog = "uiScale" }
        )

        SectionLabel("Controles de toque")
        SettingRow(
            "Mostrar toque na tela",
            if (settings.optBoolean("showTouch", true)) "Ativado" else "Desativado",
            onClick = { update("showTouch", !settings.optBoolean("showTouch", true)) }
        )
        Spacer(Modifier.height(8.dp))
        SettingRow(
            "Velocidade do ponteiro",
            String.format("%.2f", settings.optDouble("pointerSpeed", 1.0)),
            onClick = { dialog = "pointerSpeed" }
        )

        SectionLabel("Teclado")
        SettingRow(
            "Teclado virtual (IME do sistema)",
            if (settings.optBoolean("virtualKeyboard", true)) "Disponível no ambiente" else "Desativado",
            onClick = { update("virtualKeyboard", !settings.optBoolean("virtualKeyboard", true)) }
        )
        Spacer(Modifier.height(8.dp))
        SettingRow(
            "Teclado físico",
            if (settings.optBoolean("hardwareKeyboard", true)) "Ativado" else "Desativado",
            onClick = { update("hardwareKeyboard", !settings.optBoolean("hardwareKeyboard", true)) }
        )

        SectionLabel("Avançado")
        SettingRow(
            "WINEDEBUG",
            settings.optString("wineDebug", "-all"),
            onClick = { dialog = "wineDebug" }
        )
        Spacer(Modifier.height(8.dp))
        SettingRow(
            "Log do último ambiente",
            "Ver saída do bridge/sessão guest",
            onClick = {
                scope.launch(Dispatchers.IO) {
                    val envs = store.loadEnvironments()
                    logText = if (envs.isEmpty()) "nenhum ambiente criado"
                    else Runtime.bridgeLog(envs.first().id, 200).ifEmpty { "sem registros ainda" }
                    dialog = "log"
                }
            }
        )

        SectionLabel("Ambientes")
        SettingRow(
            "Gerenciar ambientes",
            "${store.loadEnvironments().size} ambiente(s)",
            onClick = { nav(Screen.Envs) }
        )

        Spacer(Modifier.height(24.dp))
    }

    // ---------------- diálogos ----------------

    when (dialog) {
        "resolution" -> NwDialog("Resolução do ambiente", onDismiss = { dialog = null },
            actions = { NwButton("Fechar", { dialog = null }, accent = false) }) {
                listOf("800x600", "1024x768", "1280x720", "1366x768", "1920x1080").forEach { r ->
                    Row(modifier = Modifier
                        .fillMaxWidth()
                        .clickable { update("resolution", r); dialog = null }
                        .padding(vertical = 8.dp)) {
                        Text(r, color = if (settings.optString("resolution", "1280x720") == r) NwOk else NwTextPrimary,
                            fontSize = 14.sp)
                    }
                }
                Text("Aplicada na próxima inicialização do ambiente.", color = NwTextSecondary, fontSize = 11.5.sp)
            }
        "renderer" -> NwDialog("Renderizador gráfico", onDismiss = { dialog = null },
            actions = { NwButton("Fechar", { dialog = null }, accent = false) }) {
                listOf("GLES2 (acelerado)" to "accelerated", "Software" to "software").forEach { (label, v) ->
                    Row(modifier = Modifier
                        .fillMaxWidth()
                        .clickable { update("renderer", v); dialog = null }
                        .padding(vertical = 8.dp)) {
                        Text(label, color = if (settings.optString("renderer", "accelerated") == v) NwOk else NwTextPrimary,
                            fontSize = 14.sp)
                    }
                }
                Text("O renderer do SDL2 usado pelo desktop do app.", color = NwTextSecondary, fontSize = 11.5.sp)
            }
        "affinity" -> AffinityDialog(
            cores = cores,
            current = settings.optInt("affinity", -1),
            onPick = { mask -> update("affinity", mask); dialog = null },
            onDismiss = { dialog = null }
        )
        "dynarec" -> NwDialog("Box64 — DynaRec", onDismiss = { dialog = null },
            actions = { NwButton("Fechar", { dialog = null }, accent = false) }) {
                listOf("Ativada (padrão, mais rápida)" to 1, "Desativada (compatibilidade)" to 0).forEach { (label, v) ->
                    Row(modifier = Modifier
                        .fillMaxWidth()
                        .clickable { update("dynarec", v); dialog = null }
                        .padding(vertical = 8.dp)) {
                        Text(label, color = if (settings.optInt("dynarec", 1) == v) NwOk else NwTextPrimary,
                            fontSize = 14.sp)
                    }
                }
                Text("DynaRec recompila código x86 em ARM64. Desativar melhora a compatibilidade e piora o desempenho.",
                    color = NwTextSecondary, fontSize = 11.5.sp)
            }
        "uiScale" -> NwDialog("Escala da interface", onDismiss = { dialog = null },
            actions = { NwButton("Fechar", { dialog = null }, accent = false) }) {
                listOf(1.0, 1.15, 1.3).forEach { v ->
                    Row(modifier = Modifier
                        .fillMaxWidth()
                        .clickable { update("uiScale", v); dialog = null }
                        .padding(vertical = 8.dp)) {
                        Text("${v}x", color = if (settings.optDouble("uiScale", 1.0) == v.toDouble()) NwOk else NwTextPrimary,
                            fontSize = 14.sp)
                    }
                }
            }
        "pointerSpeed" -> NwDialog("Velocidade do ponteiro", onDismiss = { dialog = null },
            actions = { NwButton("Fechar", { dialog = null }, accent = false) }) {
                listOf(0.5, 0.75, 1.0, 1.25, 1.5, 2.0).forEach { v ->
                    Row(modifier = Modifier
                        .fillMaxWidth()
                        .clickable { update("pointerSpeed", v); dialog = null }
                        .padding(vertical = 8.dp)) {
                        Text(String.format("%.2f", v), color = if (settings.optDouble("pointerSpeed", 1.0) == v.toDouble()) NwOk else NwTextPrimary,
                            fontSize = 14.sp)
                    }
                }
                Text("Multiplicador aplicado ao toque ao transformar em movimento do mouse.",
                    color = NwTextSecondary, fontSize = 11.5.sp)
            }
        "wineDebug" -> NwDialog("WINEDEBUG", onDismiss = { dialog = null },
            actions = { NwButton("Fechar", { dialog = null }, accent = false) }) {
                listOf("-all", "warn+all", "fixme+all", "verbose").forEach { v ->
                    Row(modifier = Modifier
                        .fillMaxWidth()
                        .clickable { update("wineDebug", v); dialog = null }
                        .padding(vertical = 8.dp)) {
                        Text(v, color = if (settings.optString("wineDebug", "-all") == v) NwOk else NwTextPrimary,
                            fontSize = 14.sp)
                    }
                }
            }
        "log" -> NwDialog("Log do ambiente", onDismiss = { dialog = null },
            actions = { NwButton("Fechar", { dialog = null }, accent = false) }) {
                Text(logText, color = NwTextSecondary, fontSize = 10.5.sp,
                    fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)
            }
        else -> {}
    }
}

private fun describeAffinity(mask: Int, cores: Int): String =
    if (mask < 0) "Todos os núcleos ($cores)"
    else "Núcleos " + (0 until cores).filter { (mask shr it) and 1 == 1 }.joinToString(", ") { "$it" }

@Composable
private fun AffinityDialog(cores: Int, current: Int, onPick: (Int) -> Unit, onDismiss: () -> Unit) {
    var mask by remember {
        mutableStateOf(if (current < 0) (1 shl cores) - 1 else current)
    }
    NwDialog("Núcleos de CPU", onDismiss = onDismiss,
        actions = { NwButton("Aplicar", { onPick(if (mask == (1 shl cores) - 1) -1 else mask) }, accent = true) }) {
        (0 until cores).forEach { c ->
            val on = (mask shr c) and 1 == 1
            Row(modifier = Modifier
                .fillMaxWidth()
                .clickable(enabled = on || mask != 0) {
                    mask = mask xor (1 shl c)
                }
                .padding(vertical = 8.dp)) {
                Text("Núcleo $c", color = if ((mask shr c) and 1 == 1) NwOk else NwTextPrimary,
                    fontSize = 14.sp)
            }
        }
        Text("Afinação de CPU aplicada ao processo da sessão (sched_setaffinity).",
            color = NwTextSecondary, fontSize = 11.5.sp)
    }
}

private fun Modifier.androidx_clickable(onClick: () -> Unit): Modifier =
    this.then(Modifier.clickable(onClick = onClick))
