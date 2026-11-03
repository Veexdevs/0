package com.novawin

import android.app.Application
import android.content.Context
import com.novawin.core.Runtime

class NovaWinApp : Application() {

    override fun onCreate() {
        super.onCreate()
        instance = this
        // Carrega o núcleo nativo (libnovawin.so) o mais cedo possível.
        try {
            Runtime.ensureInit(this)
        } catch (e: Throwable) {
            android.util.Log.e(Runtime.TAG, "falha ao carregar o núcleo nativo", e)
        }
    }

    companion object {
        lateinit var instance: NovaWinApp
            private set

        fun ctx(): Context = instance.applicationContext
    }
}
