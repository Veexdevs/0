package com.novawin.core

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * Persistência local do NovaWin (JSON em arquivo, sem dependências extras).
 *
 * - environments.json : ambientes criados (prefixos Wine separados)
 * - programs.json     : programas importados (Meus Programas / recentes)
 * - settings.json     : configurações aplicadas de verdade ao runtime
 */
class Store(context: Context) {

    private val context = context.applicationContext
    val filesDir: File = context.filesDir
    private val dataDir = File(context.filesDir, "data").apply { mkdirs() }

    private val envFile = File(dataDir, "environments.json")
    private val progFile = File(dataDir, "programs.json")
    private val setFile = File(dataDir, "settings.json")

    // ---------------- Ambientes ----------------

    fun loadEnvironments(): MutableList<Environment> {
        val list = mutableListOf<Environment>()
        try {
            val arr = JSONArray(envFile.readText())
            for (i in 0 until arr.length()) list.add(Environment.fromJson(arr.getJSONObject(i)))
        } catch (_: Exception) {
        }
        return list
    }

    fun saveEnvironments(list: List<Environment>) {
        val arr = JSONArray()
        list.forEach { arr.put(it.toJson()) }
        envFile.writeText(arr.toString(2))
    }

    // ---------------- Programas ----------------

    fun loadPrograms(): MutableList<ProgramInfo> {
        val list = mutableListOf<ProgramInfo>()
        try {
            val arr = JSONArray(progFile.readText())
            for (i in 0 until arr.length()) list.add(ProgramInfo.fromJson(arr.getJSONObject(i)))
        } catch (_: Exception) {
        }
        return list
    }

    fun savePrograms(list: List<ProgramInfo>) {
        val arr = JSONArray()
        list.forEach { arr.put(it.toJson()) }
        progFile.writeText(arr.toString(2))
    }

    fun upsertProgram(p: ProgramInfo) {
        val list = loadPrograms().toMutableList()
        list.removeAll { it.id == p.id }
        list.add(0, p)
        savePrograms(list.take(64))
    }

    fun removeProgram(id: String) {
        savePrograms(loadPrograms().filter { it.id != id })
    }

    fun recents(limit: Int = 12): List<ProgramInfo> {
        val list = loadPrograms().filter { it.lastRunAt > 0 }.sortedByDescending { it.lastRunAt }
        return list.take(limit)
    }

    // ---------------- Configurações ----------------

    fun loadSettings(): JSONObject {
        return try {
            JSONObject(setFile.readText())
        } catch (_: Exception) {
            JSONObject()
        }
    }

    fun saveSettings(o: JSONObject) {
        setFile.writeText(o.toString(2))
    }

    // ---------------- Caminhos ----------------

    fun rootfsDir(): File = File(context.filesDir, "rootfs")
    fun envDir(env: Environment): File = File(rootfsDir(), "envs/${env.dirName}")
    fun envPrefix(env: Environment): File = File(envDir(env), "prefix")
    fun envAppsDir(env: Environment): File = File(envDir(env), "apps").apply { mkdirs() }
    fun envTmpDir(env: Environment): File = File(envDir(env), "tmp").apply { mkdirs() }
}
