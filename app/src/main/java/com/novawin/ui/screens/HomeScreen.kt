package com.novawin.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.ui.draw.clip
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
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
import org.json.JSONObject

@Composable
fun HomeScreen(store: Store, nav: (Screen) -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    var prepState by remember { mutableIntStateOf(-100) }
    var prepProgress by remember { mutableStateOf(0f) }
    var info by remember { mutableStateOf<JSONObject?>(null) }
    var desktopError by remember { mutableStateOf<String?>(null) }
    var desktopBusy by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        prepState = Runtime.prepareState()
        info = Runtime.info()
    }

    fun refresh() {
        prepState = Runtime.prepareState()
        info = Runtime.info()
    }

    fun startPrepare() {
        prepState = RuntimeState.RUNNING
        scope.launch(Dispatchers.IO) {
            Runtime.prepare(
                context,
                Runtime.ProgressCb { done, total ->
                    if (total > 0) prepProgress = done.toFloat() / total
                }
            )
            refresh()
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 20.dp)
    ) {
        // ---------------- Logo ----------------
        Row(
            modifier = Modifier.fillMaxWidth().padding(top = 28.dp, bottom = 12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            NwLogo(72.dp)
            Spacer(Modifier.width(16.dp))
            Column {
                Text(
                    "NovaWin",
                    color = NwTextPrimary,
                    fontSize = 30.sp,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    "Windows no Android — Wine + Box64/Box86 + SDL2",
                    color = NwTextSecondary,
                    fontSize = 12.5.sp
                )
            }
        }

        // ---------------- Status do runtime ----------------
        NwCard(modifier = Modifier.padding(bottom = 16.dp)) {
            when (prepState) {
                RuntimeState.READY -> Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.CheckCircle, null, tint = NwOk, modifier = Modifier.size(26.dp))
                    Spacer(Modifier.width(12.dp))
                    Column(Modifier.weight(1f)) {
                        Text("Ambiente de execução pronto", color = NwTextPrimary,
                            fontSize = 15.sp, fontWeight = FontWeight.SemiBold)
                        val i = info
                        Text(
                            buildString {
                                if (i != null) {
                                    append("Wine ").append(i.optString("wine", "?"))
                                    append("  ·  Box64 ").append(i.optString("box64", "?"))
                                    append("  ·  SDL2 ").append(i.optString("sdl", "?"))
                                }
                            },
                            color = NwTextSecondary, fontSize = 12.5.sp
                        )
                    }
                }
                RuntimeState.RUNNING -> Column {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(22.dp),
                            color = NwAccent, strokeWidth = 2.dp
                        )
                        Spacer(Modifier.width(12.dp))
                        Text("Preparando ambiente Windows…", color = NwTextPrimary, fontSize = 14.5.sp)
                    }
                    Spacer(Modifier.height(10.dp))
                    LinearProgressIndicator(
                        progress = { prepProgress },
                        modifier = Modifier.fillMaxWidth(),
                        color = NwAccent,
                        trackColor = NwSurfaceAlt
                    )
                }
                else -> Column {
                    Text(
                        "Ambiente de execução ainda não preparado",
                        color = NwTextPrimary, fontSize = 15.sp, fontWeight = FontWeight.SemiBold
                    )
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "Na primeira utilização o NovaWin extrai o sistema de execução " +
                            "(rootfs Linux com Wine, Xvfb, Box64 e Box86) para o armazenamento " +
                            "privado do app. Isso pode levar alguns minutos e requer espaço livre.",
                        color = NwTextSecondary, fontSize = 12.5.sp
                    )
                    Spacer(Modifier.height(12.dp))
                    NwButton("Preparar agora", onClick = { startPrepare() })
                }
            }
        }

        // ---------------- Ações principais ----------------
        val enabled = prepState == RuntimeState.READY

        NwActionButton(
            title = "Executar EXE",
            subtitle = "Importar um arquivo .exe e executar",
            icon = Icons.Default.PlayArrow,
            onClick = { nav(Screen.Import) }
        )
        Spacer(Modifier.height(10.dp))
        NwActionButton(
            title = "Meus Programas",
            subtitle = "Programas importados e recentes",
            icon = Icons.Default.Apps,
            onClick = { nav(Screen.Programs) }
        )
        Spacer(Modifier.height(10.dp))
        NwActionButton(
            title = "Gerenciador de Arquivos",
            subtitle = "Navegar pelas pastas do ambiente Windows",
            icon = Icons.Default.Folder,
            onClick = { nav(Screen.Files) }
        )
        Spacer(Modifier.height(10.dp))
        NwActionButton(
            title = "Configurações",
            subtitle = "Resolução, GPU, CPU, toque, teclado e ambientes",
            icon = Icons.Default.Settings,
            onClick = { nav(Screen.Settings) }
        )
        Spacer(Modifier.height(10.dp))
        NwActionButton(
            title = "Ambiente Windows",
            subtitle = "Abrir a área de trabalho do ambiente",
            icon = Icons.Default.Computer,
            onClick = {
                val env = Launcher.defaultEnv(store)
                if (env == null) {
                    nav(Screen.Envs)
                } else {
                    scope.launch(Dispatchers.IO) {
                        desktopBusy = true
                        val res = if (Runtime.sessionAlive(env.id)) {
                            null
                        } else {
                            Launcher.launch(context, store, env, null, null)
                        }
                        desktopBusy = false
                        if (res == null || res.ok) {
                            Launcher.openDesktop(context, env)
                        } else {
                            desktopError = res.message + (if (res.detail.isNotBlank()) "\n\n" + res.detail else "")
                        }
                    }
                }
            },
            enabled = enabled && !desktopBusy
        )
        Spacer(Modifier.height(10.dp))
        NwActionButton(
            title = "Ambientes",
            subtitle = "Criar e gerenciar ambientes Windows (prefixos Wine)",
            icon = Icons.Default.Storage,
            onClick = { nav(Screen.Envs) }
        )

        // ---------------- Recentes ----------------
        val recents = remember { store.recents() }
        if (recents.isNotEmpty()) {
            SectionLabel("Executados recentemente")
            recents.forEach { p ->
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(14.dp))
                        .background(NwSurface)
                        .clickable {
                            val env = store.loadEnvironments().find { it.id == p.envId }
                            if (env != null) {
                                val exe = "${store.envDir(env).absolutePath}/apps/${p.fileName}"
                                scope.launch(Dispatchers.IO) {
                                    val res = Launcher.launch(context, store, env, exe, p)
                                    if (res.ok) Launcher.openDesktop(context, env)
                                }
                            }
                        }
                        .padding(horizontal = 14.dp, vertical = 11.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(Icons.Default.Description, null, tint = NwAccent,
                        modifier = Modifier.size(22.dp))
                    Spacer(Modifier.width(12.dp))
                    Column(Modifier.weight(1f)) {
                        Text(p.name, color = NwTextPrimary, fontSize = 14.sp,
                            maxLines = 1, overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis)
                        Text(
                            if (p.lastRunAt > 0) formatWhen(p.lastRunAt) else "",
                            color = NwTextSecondary, fontSize = 11.5.sp
                        )
                    }
                    Icon(Icons.Default.ChevronRight, null, tint = NwTextSecondary,
                        modifier = Modifier.size(18.dp))
                }
                Spacer(Modifier.height(6.dp))
            }
        } else {
            SectionLabel("Executados recentemente")
            NwCard {
                Text(
                    "Nenhum programa executado ainda.\nToque em “Executar EXE” para importar seu primeiro .exe.",
                    color = NwTextSecondary, fontSize = 13.sp
                )
            }
        }
        Spacer(Modifier.height(24.dp))
    }

    if (desktopError != null) {
        AlertDialog(
            onDismissRequest = { desktopError = null },
            title = { Text("Nao foi possivel abrir o ambiente") },
            text = { Text(desktopError ?: "") },
            confirmButton = { TextButton(onClick = { desktopError = null }) { Text("OK") } }
        )
    }
}

private fun formatWhen(t: Long): String {
    val diff = System.currentTimeMillis() - t
    val min = diff / 60000
    return when {
        min < 1 -> "agora mesmo"
        min < 60 -> "há ${min} min"
        min < 60 * 24 -> "há ${min / 60} h"
        else -> "há ${min / (60 * 24)} d"
    }
}
