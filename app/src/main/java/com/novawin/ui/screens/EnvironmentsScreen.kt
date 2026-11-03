package com.novawin.ui.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.novawin.Launcher
import com.novawin.RuntimeState
import com.novawin.Screen
import com.novawin.core.*
import com.novawin.ui.components.NwButton
import com.novawin.ui.components.NwCard
import com.novawin.ui.components.NwDialog
import com.novawin.ui.components.NwTextField
import com.novawin.ui.components.TopBar
import com.novawin.ui.theme.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

@Composable
fun EnvironmentsScreen(store: Store, nav: (Screen) -> Unit, back: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var envs by remember { mutableStateOf(store.loadEnvironments()) }
    var showCreate by remember { mutableStateOf(false) }
    var showDelete by remember { mutableStateOf<Environment?>(null) }
    var busy by remember { mutableStateOf(false) }
    var toast by remember { mutableStateOf("") }
    val info = remember { Runtime.info() }
    val wine32 = info.optBoolean("wine32")

    fun refresh() = envs = store.loadEnvironments()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 20.dp)
    ) {
        TopBar("Ambientes Windows", onBack = back) {
            Icon(
                Icons.Default.Add, contentDescription = "Novo ambiente", tint = NwAccent,
                modifier = Modifier.size(22.dp).clickable { showCreate = true }
            )
        }

        Text(
            "Cada ambiente usa um prefixo Wine separado (isolamento de programas), " +
                "diretório próprio dentro do rootfs e as configurações abaixo.",
            color = NwTextSecondary, fontSize = 12.5.sp
        )
        Spacer(Modifier.height(10.dp))

        if (envs.isEmpty()) {
            NwCard {
                Text("Nenhum ambiente criado.", color = NwTextSecondary, fontSize = 13.5.sp)
                Spacer(Modifier.height(10.dp))
                NwButton("Criar ambiente", onClick = { showCreate = true })
            }
        }

        envs.forEach { env ->
            NwCard(modifier = Modifier.padding(vertical = 5.dp)) {
                Column {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(env.name, color = NwTextPrimary, fontSize = 16.sp,
                            fontWeight = androidx.compose.ui.text.font.FontWeight.SemiBold,
                            modifier = Modifier.weight(1f))
                        if (env.prefixReady) {
                            Text("pronto", color = NwOk, fontSize = 11.sp,
                                modifier = Modifier
                                    .clip(RoundedCornerShape(8.dp))
                                    .background(NwAccentSoft)
                                    .padding(horizontal = 8.dp, vertical = 3.dp))
                        } else {
                            Text("prefixo pendente", color = NwTextSecondary, fontSize = 11.sp,
                                modifier = Modifier
                                    .clip(RoundedCornerShape(8.dp))
                                    .background(NwSurfaceAlt)
                                    .padding(horizontal = 8.dp, vertical = 3.dp))
                        }
                    }
                    Spacer(Modifier.height(8.dp))
                    EnvInfoRow("Arquitetura", Arch.label(env.arch))
                    EnvInfoRow("Resolução", env.resolution)
                    EnvInfoRow("Gráficos", "Software (Wine GDI)")
                    EnvInfoRow("Memória disponível",
                        "${Launcher.deviceRamMb() / 1024} GB (dispositivo)")
                    EnvInfoRow("Versão do Wine",
                        if (env.wineVersion.isNotEmpty()) env.wineVersion
                        else (info.optString("wine", "detectada na primeira inicialização")))
                    EnvInfoRow("Diretório", "rootfs/envs/${env.dirName}")
                    Spacer(Modifier.height(10.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        NwButton("Abrir desktop", accent = false,
                            modifier = Modifier.weight(1f),
                            onClick = {
                                if (Runtime.prepareState() != RuntimeState.READY) {
                                    toast = "Prepare o ambiente na tela inicial antes de abrir o desktop."
                                } else {
                                    scope.launch(Dispatchers.IO) {
                                        val res = Launcher.launch(context, store, env, null)
                                        if (res.ok) Launcher.openDesktop(context, env)
                                        else toast = res.message
                                    }
                                }
                            },
                            enabled = !busy)
                        Icon(
                            Icons.Default.Delete, null, tint = NwError,
                            modifier = Modifier.size(20.dp)
                                .clickable { showDelete = env }
                                .padding(top = 12.dp)
                        )
                    }
                }
            }
        }

        if (toast.isNotEmpty()) {
            NwCard(modifier = Modifier.padding(top = 10.dp)) {
                Text(toast, color = NwTextSecondary, fontSize = 12.5.sp)
            }
        }
        Spacer(Modifier.height(24.dp))
    }

    if (showCreate) {
        CreateEnvironmentDialog(
            info = info,
            wine32 = wine32,
            onDismiss = { showCreate = false },
            onCreate = { name, arch, resolution ->
                showCreate = false
                if (Runtime.prepareState() != RuntimeState.READY) {
                    toast = "Prepare o ambiente na tela inicial antes de criar ambientes."
                    return@CreateEnvironmentDialog
                }
                busy = true
                scope.launch(Dispatchers.IO) {
                    val slug = name.lowercase().replace(Regex("[^a-z0-9]+"), "-").trim('-')
                        .ifEmpty { "env-${System.currentTimeMillis() % 100000}" }
                    val env = Environment(
                        id = slug,
                        name = name,
                        arch = arch,
                        resolution = resolution,
                        wineVersion = info.optString("wine", ""),
                        dirName = slug
                    )
                    val (ok, err) = Runtime.envCreate(context, env)
                    if (!ok) {
                        toast = "Falha ao criar o ambiente: $err"
                    } else {
                        val list = store.loadEnvironments().toMutableList()
                        list.removeAll { it.id == env.id }
                        list.add(env)
                        store.saveEnvironments(list)
                        toast = "Ambiente criado. O prefixo Wine será inicializado no primeiro uso."
                    }
                    busy = false
                    refresh()
                }
            }
        )
    }

    showDelete?.let { env ->
        NwDialog("Excluir ambiente?", onDismiss = { showDelete = null },
            actions = {
                NwButton("Cancelar", { showDelete = null }, accent = false)
                Spacer(Modifier.width(8.dp))
                NwButton("Excluir", {
                    showDelete = null
                    busy = true
                    scope.launch(Dispatchers.IO) {
                        Runtime.sessionKill(env.id)
                        val rc = Runtime.envDelete(env.id)
                        store.saveEnvironments(store.loadEnvironments().filter { it.id != env.id })
                        refresh()
                        toast = if (rc == 0) "Ambiente excluído." else "Falha ao excluir (código $rc)."
                        busy = false
                    }
                })
            }) {
            Text(
                "O prefixo Wine, os programas importados e os arquivos de C:\\ deste ambiente " +
                    "serão apagados. Esta ação não pode ser desfeita.",
                color = NwTextSecondary, fontSize = 13.sp
            )
        }
    }
}

@Composable
private fun EnvInfoRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
        Text(label, color = NwTextSecondary, fontSize = 12.5.sp, modifier = Modifier.width(150.dp))
        Text(value, color = NwTextPrimary, fontSize = 12.5.sp, modifier = Modifier.weight(1f))
    }
}

@Composable
private fun CreateEnvironmentDialog(
    info: org.json.JSONObject,
    wine32: Boolean,
    onDismiss: () -> Unit,
    onCreate: (String, Int, String) -> Unit
) {
    var name by remember { mutableStateOf("") }
    var arch by remember { mutableIntStateOf(Arch.WIN64) }
    var resolution by remember { mutableStateOf("1280x720") }
    val resolutions = listOf("800x600", "1024x768", "1280x720", "1366x768", "1920x1080")

    NwDialog("Novo ambiente", onDismiss = onDismiss,
        actions = {
            NwButton("Criar", {
                if (name.isNotBlank()) onCreate(name.trim(), arch, resolution)
            }, enabled = name.isNotBlank())
        }) {
        NwTextField(name, { name = it }, "Nome do ambiente")
        Text("Arquitetura", color = NwTextSecondary, fontSize = 12.5.sp)
        Row(Modifier.fillMaxWidth().padding(top = 4.dp)) {
            listOf(0 to "64 bits (Box64)", 1 to "32 bits (Box86)").forEach { (a, label) ->
                Text(
                    label,
                    color = if (arch == a) NwTextPrimary else NwTextSecondary,
                    fontSize = 13.sp,
                    modifier = Modifier
                        .clip(RoundedCornerShape(10.dp))
                        .background(if (arch == a) NwAccentSoft else androidx.compose.ui.graphics.Color.Transparent)
                        .clickable(enabled = a == Arch.WIN64 || wine32) { arch = a }
                        .padding(horizontal = 12.dp, vertical = 7.dp)
                        .padding(end = 8.dp)
                )
            }
        }
        if (!wine32) {
            Text("32 bits indisponível: o rootfs atual não inclui o Wine 32 bits (wine32).",
                color = NwTextSecondary, fontSize = 11.sp)
        }
        Text("Resolução", color = NwTextSecondary, fontSize = 12.5.sp)
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 4.dp)
                .horizontalScroll(rememberScrollState())
        ) {
            resolutions.forEach { r ->
                Text(
                    r,
                    color = if (resolution == r) NwTextPrimary else NwTextSecondary,
                    fontSize = 12.5.sp,
                    modifier = Modifier
                        .clip(RoundedCornerShape(10.dp))
                        .background(if (resolution == r) NwAccentSoft else androidx.compose.ui.graphics.Color.Transparent)
                        .clickable { resolution = r }
                        .padding(horizontal = 10.dp, vertical = 6.dp)
                        .padding(end = 6.dp)
                )
            }
        }
        Spacer(Modifier.height(8.dp))
        Text(
            "O Wine usado será o do rootfs (versão ${info.optString("wine", "?")}). " +
                "A memória disponível é a do dispositivo.",
            color = NwTextSecondary, fontSize = 11.5.sp
        )
    }
}
