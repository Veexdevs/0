package com.novawin.core

import org.json.JSONObject

/** Arquiteturas suportadas (arquitetura do prefixo Wine / emulador). */
object Arch {
    const val WIN64 = 0
    const val WIN32 = 1

    fun label(a: Int): String = if (a == WIN32) "32 bits (Box86)" else "64 bits (Box64)"
}

data class Environment(
    val id: String,
    val name: String,
    val arch: Int = Arch.WIN64,
    val resolution: String = "1280x720",
    val graphics: String = "software",
    val memoryMb: Int = 0, // 0 = usa a memória disponível do dispositivo
    val wineVersion: String = "",
    val dirName: String = "",
    val createdAt: Long = System.currentTimeMillis(),
    val prefixReady: Boolean = false
)

fun Environment.toJson(): JSONObject = JSONObject().apply {
    put("id", id); put("name", name); put("arch", arch)
    put("resolution", resolution); put("graphics", graphics)
    put("memoryMb", memoryMb); put("wineVersion", wineVersion)
    put("dirName", dirName); put("createdAt", createdAt)
    put("prefixReady", prefixReady)
}

fun Environment.fromJson(o: JSONObject): Environment = Environment(
    id = o.optString("id"),
    name = o.optString("name"),
    arch = o.optInt("arch", Arch.WIN64),
    resolution = o.optString("resolution", "1280x720"),
    graphics = o.optString("graphics", "software"),
    memoryMb = o.optInt("memoryMb", 0),
    wineVersion = o.optString("wineVersion"),
    dirName = o.optString("dirName"),
    createdAt = o.optLong("createdAt"),
    prefixReady = o.optBoolean("prefixReady")
)

data class ProgramInfo(
    val id: String,
    val envId: String,
    val name: String,
    val fileName: String,
    val size: Long = 0,
    val arch: Int = -1, // -1 desconhecido, 32 ou 64
    val addedAt: Long = System.currentTimeMillis(),
    val lastRunAt: Long = 0
)

fun ProgramInfo.toJson(): JSONObject = JSONObject().apply {
    put("id", id); put("envId", envId); put("name", name)
    put("fileName", fileName); put("size", size)
    put("arch", arch); put("addedAt", addedAt); put("lastRunAt", lastRunAt)
}

fun ProgramInfo.fromJson(o: JSONObject): ProgramInfo = ProgramInfo(
    id = o.optString("id"),
    envId = o.optString("envId"),
    name = o.optString("name"),
    fileName = o.optString("fileName"),
    size = o.optLong("size"),
    arch = o.optInt("arch", -1),
    addedAt = o.optLong("addedAt"),
    lastRunAt = o.optLong("lastRunAt")
)

data class FileEntry(
    val name: String,
    val path: String,
    val isDirectory: Boolean,
    val size: Long,
    val mtime: Long
)

data class ProcInfo(
    val pid: Int,
    val name: String,
    val state: String,
    val isGuest: Boolean
)

data class GuestWindow(
    val id: Int,
    val title: String,
    val active: Boolean,
    val fullscreen: Boolean
)

/** Resultado de uma operação de execução. */
data class LaunchResult(
    val ok: Boolean,
    val message: String,
    val detail: String = "",
    val pid: Int = -1
)
