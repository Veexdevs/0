package com.novawin.core

import android.content.Context
import android.net.Uri
import java.io.File

/**
 * Importação de arquivos .exe via SAF (Storage Access Framework) e
 * validação real do cabeçalho PE.
 */
object Importer {

    /** Resultado da validação PE. */
    data class PeInfo(val valid: Boolean, val arch: Int, val reason: String = "") {
        companion object {
            const val ARCH_32 = 32
            const val ARCH_64 = 64
        }
    }

    /**
     * Valida se [file] é um executável Windows (PE).
     * Lê: magic "MZ" (offset 0), offset do cabeçalho PE (0x3C),
     * magic "PE\0\0" e a máquina alvo (0x8664 = x86-64, 0x14C = i386).
     */
    fun validatePe(file: File): PeInfo {
        if (!file.exists() || file.length() < 64) {
            return PeInfo(false, -1, "arquivo muito pequeno para ser um executável PE")
        }
        return try {
            file.inputStream().use { input ->
                val head = ByteArray(64)
                var read = 0
                while (read < 64) {
                    val n = input.read(head, read, 64 - read)
                    if (n < 0) break
                    read += n
                }
                if (read < 64 || head[0].toInt() != 'M'.code || head[1].toInt() != 'Z'.code) {
                    return PeInfo(false, -1, "magic MZ ausente (não é um executável Windows)")
                }
                val peOff = ((head[0x3C].toInt() and 0xFF) or
                    ((head[0x3D].toInt() and 0xFF) shl 8) or
                    ((head[0x3E].toInt() and 0xFF) shl 16) or
                    ((head[0x3F].toInt() and 0xFF) shl 24))
                if (peOff <= 0 || peOff + 6 >= head.size) {
                    return PeInfo(false, -1, "deslocamento do cabeçalho PE inválido")
                }
                if (head[peOff].toInt() != 'P'.code || head[peOff + 1].toInt() != 'E'.code ||
                    head[peOff + 2].toInt() != 0 || head[peOff + 3].toInt() != 0
                ) {
                    return PeInfo(false, -1, "cabeçalho PE ausente")
                }
                val machine = (head[peOff + 4].toInt() and 0xFF) or
                    ((head[peOff + 5].toInt() and 0xFF) shl 8)
                when (machine) {
                    0x8664 -> return PeInfo(true, PeInfo.ARCH_64)
                    0x014C -> return PeInfo(true, PeInfo.ARCH_32)
                    else -> return PeInfo(false, -1, "arquitetura PE não suportada (0x${Integer.toHexString(machine)})")
                }
            }
        } catch (e: Exception) {
            PeInfo(false, -1, "erro ao ler o arquivo: ${e.message}")
        }
    }

    /**
     * Copia [uri] (SAF) para [dest]. Retorna true em sucesso.
     * Chamar em thread de background; [progress] recebe (copiado, total).
     */
    fun copyFromUri(
        context: Context,
        uri: Uri,
        dest: File,
        progress: ((Long, Long) -> Unit)? = null
    ): Boolean {
        try {
            val resolver = context.contentResolver
            val total: Long = resolver.openAssetFileDescriptor(uri, "r")?.length ?: 0L
            var done = 0L
            val input = resolver.openInputStream(uri) ?: return false
            input.use { stream ->
                dest.outputStream().use { output ->
                    val buf = ByteArray(128 * 1024)
                    while (true) {
                        val n = stream.read(buf)
                        if (n < 0) break
                        output.write(buf, 0, n)
                        done += n
                        progress?.invoke(done, total)
                    }
                    output.flush()
                }
            }
            return true
        } catch (e: Exception) {
            dest.delete()
            return false
        }
    }

    fun sanitizeName(name: String): String {
        val clean = name.replace(Regex("[^A-Za-z0-9._-]"), "_")
        return if (clean.isEmpty()) "programa" else clean
    }
}
