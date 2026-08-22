package com.oledrelay.app

import android.content.Context

object Prefs {
    private const val NAME = "oled_relay_prefs"
    private const val KEY_DEVICE_IP = "device_ip"
    private const val KEY_ONLY_APPS = "only_apps"
    private const val KEY_IGNORE_APPS = "ignore_apps"
    private const val KEY_DURATION = "duration_ms"

    fun deviceIp(ctx: Context): String =
        ctx.getSharedPreferences(NAME, Context.MODE_PRIVATE).getString(KEY_DEVICE_IP, "") ?: ""

    fun onlyApps(ctx: Context): String =
        ctx.getSharedPreferences(NAME, Context.MODE_PRIVATE).getString(KEY_ONLY_APPS, "") ?: ""

    fun ignoreApps(ctx: Context): String =
        ctx.getSharedPreferences(NAME, Context.MODE_PRIVATE).getString(KEY_IGNORE_APPS, "") ?: ""

    fun durationMs(ctx: Context): Int =
        ctx.getSharedPreferences(NAME, Context.MODE_PRIVATE).getInt(KEY_DURATION, 6000)

    fun save(ctx: Context, deviceIp: String, onlyApps: String, ignoreApps: String, durationMs: Int) {
        ctx.getSharedPreferences(NAME, Context.MODE_PRIVATE).edit()
            .putString(KEY_DEVICE_IP, deviceIp.trim())
            .putString(KEY_ONLY_APPS, onlyApps.trim())
            .putString(KEY_IGNORE_APPS, ignoreApps.trim())
            .putInt(KEY_DURATION, durationMs)
            .apply()
    }
}
