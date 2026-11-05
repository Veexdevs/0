package com.novawin.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
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
import com.novawin.Screen
import com.novawin.core.*
import com.novawin.ui.components.NwCard
import com.novawin.ui.components.NwButton
import com.novawin.ui.components.TopBar
import com.novawin.ui.theme.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

@Composable
fun ProgramsScreen(store: Store, nav: (Screen) -> Unit, back: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var programs by remember { mutableStateOf(store.loadPrograms()) }
    var menuFor by remember { mutableStateOf<String?>(null) }
    var launching by remember { mutableStateOf<String?>(null) }
    var toast by remember { mutableStateOf("") }

    fun launch(p: ProgramInfo) {
        launching = p.id
        scope.launch(Dispatchers.IO) {
            val env = store.loadEnvironments().find { it.id == p.envId }
            if (env == null) {
                toast = "Ambiente do programa não existe mais."
                launching = null
                return@launch
            }
            val exe = "${store.envDir(env).absolutePath}/apps/${p.fileName}"
            val res = Launcher.launch(context, store, env, exe, p)
            if (res.ok) {
                Launcher.openDesktop(context, env)
            } else {
                toast = res.message
            }
            launching = null
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 20.dp)
    ) {
        TopBar("Meus Programas", onBack = back)

        if (programs.isEmpty()) {
            NwCard {
                Text(
                    "Nenhum programa importado.\nToque em “Executar EXE” na tela inicial para importar um arquivo .exe.",
                    color = NwTextSecondary, fontSize = 13.5.sp
                )
            }
        }

        programs.forEach { p ->
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(14.dp))
                    .background(com.novawin.ui.theme.NwCard)
                    .clickable { if (launching == null) launch(p) }
                    .padding(14.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Icon(Icons.Default.Description, null, tint = NwAccent,
                    modifier = Modifier.size(26.dp))
                Spacer(Modifier.width(12.dp))
                Column(Modifier.weight(1f)) {
                    Text(p.name, color = NwTextPrimary, fontSize = 14.5.sp,
                        maxLines = 1, overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis)
                    Text(
                        when (p.arch) {
                            Importer.PeInfo.ARCH_64 -> "64 bits"
                            Importer.PeInfo.ARCH_32 -> "32 bits"
                            else -> ""
                        }.let { if (it.isEmpty()) "" else "$it · " } +
                            "${(p.size / 1024 / 1024)} MB",
                        color = NwTextSecondary, fontSize = 12.sp
                    )
                }
                if (launching == p.id) {
                    Text("…", color = NwAccent, fontSize = 16.sp)
                    Spacer(Modifier.width(6.dp))
                }
                Box {
                    Icon(Icons.Default.MoreVert, null, tint = NwTextSecondary,
                        modifier = Modifier.size(20.dp).clickable { menuFor = p.id })
                    DropdownMenu(
                        expanded = menuFor == p.id,
                        onDismissRequest = { menuFor = null },
                        containerColor = NwSurface
                    ) {
                        DropdownMenuItem(
                            text = { Text("Abrir pasta do programa", color = NwTextPrimary) },
                            onClick = {
                                menuFor = null
                                nav(Screen.Files)
                            }
                        )
                        DropdownMenuItem(
                            text = { Text("Remover da lista", color = NwError) },
                            onClick = {
                                menuFor = null
                                store.removeProgram(p.id)
                                programs = store.loadPrograms()
                                toast = "Programa removido da lista (o arquivo continua no ambiente)."
                            }
                        )
                    }
                }
            }
            Spacer(Modifier.height(8.dp))
        }

        if (toast.isNotEmpty()) {
            Spacer(Modifier.height(8.dp))
            NwCard {
                Text(toast, color = NwTextSecondary, fontSize = 13.sp)
            }
        }
        Spacer(Modifier.height(24.dp))
    }
}
