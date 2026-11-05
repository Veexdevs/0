package com.novawin.ui.screens

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.*
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
import com.novawin.Screen
import com.novawin.core.Environment
import com.novawin.core.Runtime
import com.novawin.core.Store
import com.novawin.ui.components.NwCard
import com.novawin.ui.components.TopBar
import com.novawin.ui.theme.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import java.io.File

private enum class RootId { DRIVE_C, ENV, ANDROID }

@Composable
fun FileManagerScreen(store: Store, nav: (Screen) -> Unit, back: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    val envs = remember { store.loadEnvironments() }
    val env: Environment = envs.firstOrNull() ?: run {
        // sem ambiente: mostra apenas o seletor de pasta do Android
        Environment(id = "none", name = "sem ambiente", dirName = "")
    }

    var root by remember { mutableStateOf(RootId.DRIVE_C) }
    var pathStack by remember {
        mutableStateOf(mutableListOf(
            if (store.envPrefix(env).exists()) "${store.envPrefix(env).absolutePath}/drive_c"
            else store.envPrefix(env).absolutePath
        ))
    }
    var androidTree by remember { mutableStateOf<Uri?>(null) }
    var entries by remember { mutableStateOf<List<com.novawin.core.FileEntry>>(emptyList()) }
    var loading by remember { mutableStateOf(false) }
    var clipboard by remember {
        mutableStateOf<Pair<String, Boolean>?>(null) // (caminho, true=copiar)
    }
    var toast by remember { mutableStateOf("") }
    var envReady by remember { mutableStateOf(env.id != "none") }
    var menuFor by remember { mutableStateOf<String?>(null) }

    val currentPath: String = pathStack.last()

    fun reload() {
        if (!envReady) {
            entries = emptyList()
            return
        }
        loading = true
        scope.launch(Dispatchers.IO) {
            val list = Runtime.fsList(currentPath)
            entries = list.sortedBy { if (it.isDirectory) "0" else "1" + it.name.lowercase() }
            loading = false
        }
    }

    LaunchedEffect(root, currentPath, envReady) { reload() }

    val importPicker = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        if (uri == null) return@rememberLauncherForActivityResult
        val destDir = if (root == RootId.DRIVE_C) currentPath
        else "${store.envDir(env).absolutePath}/apps"
        scope.launch(Dispatchers.IO) {
            val name = uri.toString().substringAfterLast('/')
            val ok = com.novawin.core.Importer.copyFromUri(
                context, uri, File(destDir, name)
            )
            toast = if (ok) "Arquivo importado." else "Falha ao importar o arquivo."
            reload()
        }
    }

    fun reloadAndroid(uri: Uri) {
        scope.launch(Dispatchers.IO) {
            val list = mutableListOf<com.novawin.core.FileEntry>()
            try {
                val children = androidx.documentfile.provider.DocumentFile
                    .fromTreeUri(context, uri)?.listFiles()
                children?.forEach { df ->
                    list.add(
                        com.novawin.core.FileEntry(
                            df.name ?: "?",
                            df.uri.toString(),
                            df.isDirectory,
                            df.length(),
                            df.lastModified()
                        )
                    )
                }
            } catch (_: Exception) {
            }
            entries = list.sortedBy { if (it.isDirectory) "0" else "1" + it.name.lowercase() }
        }
    }

    val treePicker = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocumentTree()
    ) { uri: Uri? ->
        if (uri != null) {
            try {
                context.contentResolver.takePersistableUriPermission(
                    uri,
                    android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION
                )
            } catch (_: Exception) {
            }
            androidTree = uri
            root = RootId.ANDROID
            reloadAndroid(uri)
        }
    }

    fun doAction(action: String, entry: com.novawin.core.FileEntry) {
        when (action) {
            "delete" -> scope.launch(Dispatchers.IO) {
                val rc = Runtime.fsDelete(entry.path)
                toast = if (rc == 0) "Excluído." else "Não foi possível excluir (código $rc)."
                reload()
            }
            "copy" -> {
                clipboard = entry.path to true
                toast = "Selecionado para copiar. Toque em uma pasta para colar."
            }
            "move" -> {
                clipboard = entry.path to false
                toast = "Selecionado para mover. Toque em uma pasta para colar."
            }
        }
    }

    fun pasteTo(entry: com.novawin.core.FileEntry) {
        val clip = clipboard ?: return
        if (root == RootId.ANDROID) {
            toast = "Não é possível colar na pasta do Android (limitação do SAF). Use “Importar para cá”."
            return
        }
        if (!entry.isDirectory) {
            toast = "Escolha uma pasta de destino."
            return
        }
        val (src, isCopy) = clip
        scope.launch(Dispatchers.IO) {
            val dst = "${entry.path}/${src.substringAfterLast('/')}"
            val rc = if (isCopy) Runtime.fsCopy(src, dst) else Runtime.fsMove(src, dst)
            toast = if (rc == 0) "Concluído." else "Falha na operação (código $rc)."
            clipboard = null
            reload()
        }
    }

    fun onEntryTap(entry: com.novawin.core.FileEntry) {
        when (root) {
            RootId.ANDROID -> {
                if (entry.isDirectory && androidTree != null) {
                    reloadAndroid(Uri.parse(entry.path))
                } else if (clipboard != null) {
                    pasteTo(entry)
                }
            }
            else -> {
                if (entry.isDirectory) {
                    val clip = clipboard
                    if (clip != null) {
                        pasteTo(entry)
                    } else {
                        pathStack.add(entry.path)
                    }
                } else if (clipboard != null) {
                    pasteTo(entry)
                }
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(horizontal = 20.dp)
    ) {
        TopBar("Gerenciador de Arquivos", onBack = back)

        // ---------------- raízes ----------------
        Row(
            modifier = Modifier.fillMaxWidth().padding(top = 8.dp, bottom = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            val roots = listOf(
                Triple(RootId.DRIVE_C, "C:\\ (Windows)", envReady),
                Triple(RootId.ENV, "Ambiente", envReady),
                Triple(RootId.ANDROID, "Android", androidTree != null)
            )
            roots.forEach { (id, label, enabled) ->
                Text(
                    label,
                    color = if (root == id) NwTextPrimary else NwTextSecondary,
                    fontSize = 13.sp,
                    modifier = Modifier
                        .weight(1f)
                        .clip(RoundedCornerShape(10.dp))
                        .background(if (root == id) NwAccentSoft else androidx.compose.ui.graphics.Color.Transparent)
                        .then(if (enabled) Modifier.clickable {
                            root = id
                            if (id == RootId.DRIVE_C) {
                                pathStack = mutableListOf(
                                    if (store.envPrefix(env).exists()) "${store.envPrefix(env).absolutePath}/drive_c"
                                    else store.envPrefix(env).absolutePath
                                )
                            } else if (id == RootId.ENV) {
                                pathStack = mutableListOf(store.envDir(env).absolutePath)
                            } else if (androidTree != null) {
                                reloadAndroid(androidTree!!)
                            }
                        } else Modifier)
                        .padding(horizontal = 8.dp, vertical = 8.dp)
                )
            }
        }

        if (!envReady) {
            NwCard {
                Text(
                    "Crie um ambiente Windows para navegar em C:\\ e nas pastas do ambiente. " +
                        "A pasta do Android (SAF) pode ser usada sem ambiente.",
                    color = NwTextSecondary, fontSize = 13.sp
                )
                Spacer(Modifier.height(10.dp))
                com.novawin.ui.components.NwButton("Criar ambiente", onClick = { nav(Screen.Envs) }, accent = false)
            }
        }

        // ---------------- barra de caminho ----------------
        if (root != RootId.ANDROID && envReady) {
            val home = if (root == RootId.DRIVE_C) "C:\\" else store.envDir(env).absolutePath
            val rel = currentPath.removePrefix(home).removePrefix("/")
            Row(
                modifier = Modifier.fillMaxWidth()
                    .clip(RoundedCornerShape(10.dp)).background(NwSurface)
                    .padding(horizontal = 10.dp, vertical = 7.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(home, color = NwAccent, fontSize = 12.sp,
                    modifier = Modifier.clickable {
                        pathStack = mutableListOf(home)
                    })
                if (rel.isNotEmpty()) {
                    Text(" / $rel", color = NwTextSecondary, fontSize = 12.sp)
                }
            }
            Spacer(Modifier.height(8.dp))
        }

        // ---------------- banner de cópia/movimento ----------------
        clipboard?.let { (src, isCopy) ->
            Row(
                modifier = Modifier.fillMaxWidth()
                    .clip(RoundedCornerShape(10.dp)).background(NwAccentSoft)
                    .padding(horizontal = 10.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "${if (isCopy) "Copiar" else "Mover"}: ${src.substringAfterLast('/')} — toque em uma pasta",
                    color = NwTextPrimary, fontSize = 12.5.sp, modifier = Modifier.weight(1f)
                )
                Icon(Icons.Default.Close, null, tint = NwTextPrimary,
                    modifier = Modifier.size(18.dp).clickable { clipboard = null })
            }
            Spacer(Modifier.height(8.dp))
        }

        // ---------------- ações ----------------
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            ToolAction("Nova pasta", Icons.Default.CreateNewFolder) {
                if (!envReady || root == RootId.ANDROID) return@ToolAction
                val name = "nova_pasta" + (pathStack.size - 1)
                scope.launch(Dispatchers.IO) {
                    val rc = Runtime.fsMkdir("$currentPath/$name")
                    toast = if (rc == 0) "Pasta criada." else "Falha ao criar pasta (código $rc)."
                    reload()
                }
            }
            ToolAction("Importar", Icons.Default.FileUpload) {
                importPicker.launch(arrayOf("*/*"))
            }
            ToolAction("Pasta do Android", Icons.Default.FolderOpen) {
                treePicker.launch(null)
            }
        }
        Spacer(Modifier.height(10.dp))

        // ---------------- lista ----------------
        if (loading) {
            Row(verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp)) {
                androidx.compose.material3.CircularProgressIndicator(
                    modifier = Modifier.size(18.dp), color = NwAccent, strokeWidth = 2.dp)
                Spacer(Modifier.width(10.dp))
                Text("carregando…", color = NwTextSecondary, fontSize = 12.5.sp)
            }
        }
        LazyColumn(modifier = Modifier.weight(1f)) {
            item {
                if (pathStack.size > 1 || root == RootId.ANDROID) {
                    Row(
                        modifier = Modifier.fillMaxWidth().clickable {
                            if (root == RootId.ANDROID) {
                                androidTree?.let { reloadAndroid(it) }
                            } else {
                                pathStack.removeAt(pathStack.size - 1)
                            }
                        }.padding(vertical = 8.dp, horizontal = 4.dp)
                    ) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, null, tint = NwTextSecondary, modifier = Modifier.size(20.dp))
                        Spacer(Modifier.width(8.dp))
                        Text("..", color = NwTextSecondary, fontSize = 14.sp)
                    }
                }
            }
            items(entries) { entry ->
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(12.dp))
                        .background(NwSurface)
                        .clickable { onEntryTap(entry) }
                        .padding(12.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        if (entry.isDirectory) Icons.Default.Folder else Icons.Default.InsertDriveFile,
                        null,
                        tint = if (entry.isDirectory) NwAccent else NwTextSecondary,
                        modifier = Modifier.size(22.dp)
                    )
                    Spacer(Modifier.width(10.dp))
                    Column(Modifier.weight(1f)) {
                        Text(entry.name, color = NwTextPrimary, fontSize = 13.5.sp,
                            maxLines = 1, overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis)
                        if (!entry.isDirectory && root != RootId.ANDROID) {
                            Text(formatSizeFs(entry.size), color = NwTextSecondary, fontSize = 11.5.sp)
                        }
                    }
                    if (root != RootId.ANDROID) {
                        Box {
                            Icon(Icons.Default.MoreVert, null, tint = NwTextSecondary,
                                modifier = Modifier.size(18.dp).clickable { menuFor = entry.path })
                            DropdownMenu(
                                expanded = menuFor == entry.path,
                                onDismissRequest = { menuFor = null },
                                containerColor = NwSurface
                            ) {
                                DropdownMenuItem(
                                    text = { Text("Copiar", color = NwTextPrimary) },
                                    onClick = { menuFor = null; doAction("copy", entry) }
                                )
                                DropdownMenuItem(
                                    text = { Text("Mover", color = NwTextPrimary) },
                                    onClick = { menuFor = null; doAction("move", entry) }
                                )
                                DropdownMenuItem(
                                    text = { Text("Excluir", color = NwError) },
                                    onClick = { menuFor = null; doAction("delete", entry) }
                                )
                            }
                        }
                    }
                }
                Spacer(Modifier.height(4.dp))
            }
        }

        if (toast.isNotEmpty()) {
            Spacer(Modifier.height(6.dp))
            NwCard { Text(toast, color = NwTextSecondary, fontSize = 12.5.sp) }
        }
    }
}

@Composable
private fun RowScope.ToolAction(label: String, icon: androidx.compose.ui.graphics.vector.ImageVector, onClick: () -> Unit) {
    Column(
        modifier = Modifier
            .weight(1f)
            .clip(RoundedCornerShape(12.dp))
            .background(NwSurface)
            .clickable(onClick = onClick)
            .padding(vertical = 10.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Icon(icon, null, tint = NwAccent, modifier = Modifier.size(20.dp))
        Spacer(Modifier.height(4.dp))
        Text(label, color = NwTextPrimary, fontSize = 11.5.sp)
    }
}

private fun formatSizeFs(bytes: Long): String {
    return when {
        bytes < 1024 -> "$bytes B"
        bytes < 1024L * 1024 -> "${bytes / 1024} KB"
        else -> String.format("%.1f MB", bytes / (1024.0 * 1024.0))
    }
}
