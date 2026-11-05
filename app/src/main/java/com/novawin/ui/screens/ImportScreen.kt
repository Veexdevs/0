package com.novawin.ui.screens

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.ui.draw.clip
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.novawin.Launcher
import com.novawin.RuntimeState
import com.novawin.Screen
import com.novawin.core.*
import com.novawin.ui.components.*
import com.novawin.ui.theme.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import java.io.File

private enum class Step { CHOOSE, IMPORTING, CONFIG, LAUNCHING, ERROR }

@Composable
fun ImportScreen(store: Store, nav: (Screen) -> Unit, back: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    var step by remember { mutableStateOf(Step.CHOOSE) }
    var envs by remember { mutableStateOf(store.loadEnvironments()) }
    var envIndex by remember { mutableIntStateOf(0) }
    var resolution by remember { mutableStateOf(store.loadSettings().optString("resolution", "1280x720")) }
    var progress by remember { mutableStateOf(0f) }
    var fileName by remember { mutableStateOf("") }
    var fileArch by remember { mutableIntStateOf(-1) }
    var fileSize by remember { mutableLongStateOf(0) }
    var tempFile by remember { mutableStateOf<File?>(null) }
    var errorText by remember { mutableStateOf("") }
    var errorDetail by remember { mutableStateOf("") }
    var logLines by remember { mutableStateOf(mutableListOf<String>()) }

    val resolutions = listOf("800x600", "1024x768", "1280x720", "1366x768", "1920x1080")

    fun currentEnv(): Environment? = envs.getOrNull(envIndex)

    fun startImport(uri: Uri) {
        val env = currentEnv()
        if (env == null) {
            step = Step.ERROR
            errorText = "Crie um ambiente primeiro"
            errorDetail = "Toque em “Ambientes” na tela inicial para criar seu primeiro ambiente Windows."
            return
        }
        step = Step.IMPORTING
        scope.launch(Dispatchers.IO) {
            // 1) importa para o diretório privado do app (fora do rootfs ainda)
            val name = Importer.sanitizeName(uri.toString().substringAfterLast('/'))
                .let { if (it.endsWith(".exe", true)) it else "$it.exe" }
            val tmp = File(context.cacheDir, "import").apply { mkdirs() }
            val dest = File(tmp, name)
            val ok = Importer.copyFromUri(context, uri, dest) { done, total ->
                if (total > 0) progress = done.toFloat() / total
            }
            if (!ok) {
                errorText = "Falha ao copiar o arquivo"
                errorDetail = "Não foi possível ler o arquivo selecionado pelo gerenciador do Android."
                step = Step.ERROR
                return@launch
            }
            // 2) valida o formato PE (real)
            val pe = Importer.validatePe(dest)
            if (!pe.valid) {
                dest.delete()
                errorText = "Arquivo inválido"
                errorDetail = pe.reason
                step = Step.ERROR
                return@launch
            }
            // 3) move para dentro do ambiente (apps do usuário ficam no próprio prefixo)
            val appsDir = store.envAppsDir(env)
            val final = File(appsDir, name)
            if (!dest.renameTo(final)) {
                val copiou = try { dest.copyTo(final, overwrite = true); true } catch (_: Exception) { false }
                if (!copiou) {
                    errorText = "Falha ao mover o arquivo para o ambiente"
                    errorDetail = "Verifique o espaço livre em armazenamento."
                    step = Step.ERROR
                    return@launch
                }
            }
            fileName = final.nameWithoutExtension
            fileArch = pe.arch
            fileSize = final.length()
            tempFile = final
            step = Step.CONFIG
        }
    }

    fun launchProgram() {
        val env = currentEnv() ?: return
        val file = tempFile ?: return
        step = Step.LAUNCHING
        logLines = mutableListOf()
        scope.launch(Dispatchers.IO) {
            // atualiza registro de programas
            val program = ProgramInfo(
                id = env.id + "/" + file.name,
                envId = env.id,
                name = file.nameWithoutExtension,
                fileName = file.name,
                size = file.length(),
                arch = fileArch
            )
            val res = Launcher.launch(context, store, env, file.absolutePath, program)
            if (res.ok) {
                Launcher.openDesktop(context, env)
                back()
            } else {
                errorText = res.message
                errorDetail = res.detail
                step = Step.ERROR
            }
        }
    }

    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) startImport(uri)
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 20.dp)
    ) {
        TopBar("Executar EXE", onBack = back)

        when (step) {
            Step.CHOOSE -> {
                NwCard {
                    Column {
                        Text("Importar arquivo .exe", color = NwTextPrimary, fontSize = 16.sp,
                            fontWeight = androidx.compose.ui.text.font.FontWeight.SemiBold)
                        Spacer(Modifier.height(6.dp))
                        Text(
                            "O seletor de arquivos do Android será aberto. Escolha o executável; " +
                                "ele será copiado para o ambiente e executado com Wine + Box64/Box86.",
                            color = NwTextSecondary, fontSize = 13.sp
                        )
                        Spacer(Modifier.height(14.dp))
                        NwButton("Escolher arquivo .exe", onClick = { picker.launch(arrayOf("*/*")) })
                    }
                }
                Spacer(Modifier.height(16.dp))
                if (envs.isEmpty()) {
                    NwCard {
                        Text(
                            "Nenhum ambiente Windows criado ainda. Crie um ambiente antes de importar programas.",
                            color = NwError, fontSize = 13.5.sp
                        )
                        Spacer(Modifier.height(10.dp))
                        NwButton("Criar ambiente", onClick = { nav(Screen.Envs) }, accent = false)
                    }
                } else {
                    NwCard {
                        Column {
                            Text("Ambiente de destino", color = NwTextPrimary, fontSize = 14.5.sp,
                                fontWeight = androidx.compose.ui.text.font.FontWeight.SemiBold)
                            Spacer(Modifier.height(8.dp))
                            envs.forEachIndexed { i, e ->
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(vertical = 3.dp)
                                        .clip(RoundedCornerShape(12.dp))
                                        .background(if (i == envIndex) NwAccentSoft else androidx.compose.ui.graphics.Color.Transparent)
                                        .clickable { envIndex = i }
                                        .padding(horizontal = 12.dp, vertical = 10.dp),
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Text(e.name, color = NwTextPrimary, fontSize = 14.sp,
                                        modifier = Modifier.weight(1f))
                                    Text(Arch.label(e.arch), color = NwTextSecondary, fontSize = 12.sp)
                                }
                            }
                        }
                    }
                }
            }

            Step.IMPORTING -> {
                NwCard {
                    Column {
                        Text("Importando arquivo…", color = NwTextPrimary, fontSize = 15.sp,
                            fontWeight = androidx.compose.ui.text.font.FontWeight.SemiBold)
                        Spacer(Modifier.height(12.dp))
                        LinearProgressIndicator(
                            progress = { progress },
                            modifier = Modifier.fillMaxWidth(),
                            color = NwAccent, trackColor = NwSurfaceAlt
                        )
                    }
                }
            }

            Step.CONFIG -> {
                val env = currentEnv()
                NwCard {
                    Column {
                        Text("Configurar execução", color = NwTextPrimary, fontSize = 16.sp,
                            fontWeight = androidx.compose.ui.text.font.FontWeight.SemiBold)
                        Spacer(Modifier.height(10.dp))
                        InfoRow("Arquivo", "${fileName}.exe")
                        InfoRow("Arquitetura do programa",
                            when (fileArch) {
                                Importer.PeInfo.ARCH_64 -> "64 bits (x86-64)"
                                Importer.PeInfo.ARCH_32 -> "32 bits (x86)"
                                else -> "desconhecida"
                            })
                        InfoRow("Tamanho", formatSize(fileSize))
                        InfoRow("Ambiente", env?.name ?: "-")
                        if (env != null && fileArch == Importer.PeInfo.ARCH_32 && env.arch == Arch.WIN64) {
                            Spacer(Modifier.height(8.dp))
                            Text(
                                "Programa de 32 bits em ambiente de 64 bits: funciona apenas se o Wine " +
                                    "32 bits (wow64) estiver disponível no rootfs.",
                                color = NwError, fontSize = 12.sp
                            )
                        }
                        Spacer(Modifier.height(12.dp))
                        Text("Resolução da sessão", color = NwTextSecondary, fontSize = 12.5.sp)
                        Row(
                            modifier = Modifier.fillMaxWidth().padding(top = 6.dp)
                                .horizontalScroll(rememberScrollState())
                        ) {
                            resolutions.forEach { r ->
                                Text(
                                    r,
                                    color = if (r == resolution) NwTextPrimary else NwTextSecondary,
                                    fontSize = 13.sp,
                                    modifier = Modifier
                                        .clip(RoundedCornerShape(10.dp))
                                        .background(if (r == resolution) NwAccentSoft else androidx.compose.ui.graphics.Color.Transparent)
                                        .clickable { resolution = r }
                                        .padding(horizontal = 12.dp, vertical = 7.dp)
                                        .padding(end = 6.dp)
                                )
                            }
                        }
                        Spacer(Modifier.height(14.dp))
                        NwButton("Iniciar programa", onClick = { launchProgram() })
                    }
                }
            }

            Step.LAUNCHING -> {
                NwCard {
                    Column {
                        Text("Iniciando o programa…", color = NwTextPrimary, fontSize = 15.sp,
                            fontWeight = androidx.compose.ui.text.font.FontWeight.SemiBold)
                        Spacer(Modifier.height(6.dp))
                        Text(
                            "Preparando prefixo Wine e iniciando a sessão (Box64/Box86 + Wine). " +
                                "Na primeira execução de um ambiente isso pode demorar alguns minutos.",
                            color = NwTextSecondary, fontSize = 12.5.sp
                        )
                        if (logLines.isNotEmpty()) {
                            Spacer(Modifier.height(10.dp))
                            Box(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clip(RoundedCornerShape(10.dp))
                                    .background(NwBackground)
                                    .padding(10.dp)
                            ) {
                                Text(logLines.takeLast(6).joinToString("\n"),
                                    color = NwTextSecondary, fontSize = 10.5.sp,
                                    fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)
                            }
                        }
                    }
                }
            }

            Step.ERROR -> {
                NwCard {
                    Row(verticalAlignment = Alignment.Top) {
                        Icon(Icons.Default.Close, null, tint = NwError, modifier = Modifier.size(22.dp))
                        Spacer(Modifier.width(12.dp))
                        Column(Modifier.weight(1f)) {
                            Text(errorText, color = NwError, fontSize = 15.sp,
                                fontWeight = androidx.compose.ui.text.font.FontWeight.SemiBold)
                            if (errorDetail.isNotEmpty()) {
                                Spacer(Modifier.height(6.dp))
                                Text(errorDetail, color = NwTextSecondary, fontSize = 13.sp)
                            }
                        }
                    }
                    Spacer(Modifier.height(14.dp))
                    NwButton("Tentar novamente", onClick = {
                        step = Step.CHOOSE
                        tempFile?.delete()
                    }, accent = false)
                }
            }
        }
        Spacer(Modifier.height(24.dp))
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp)
    ) {
        Text(label, color = NwTextSecondary, fontSize = 13.sp, modifier = Modifier.width(160.dp))
        Text(value, color = NwTextPrimary, fontSize = 13.sp, modifier = Modifier.weight(1f))
    }
}

private fun formatSize(bytes: Long): String {
    return when {
        bytes < 1024 -> "$bytes B"
        bytes < 1024L * 1024 -> "${bytes / 1024} KB"
        else -> String.format("%.1f MB", bytes / (1024.0 * 1024.0))
    }
}
