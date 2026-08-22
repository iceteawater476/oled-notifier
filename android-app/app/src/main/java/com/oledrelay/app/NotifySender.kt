package com.oledrelay.app

import android.util.Log
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder

object NotifySender {
    private const val TAG = "NotifySender"

    fun send(deviceIp: String, app: String, title: String, message: String, file: String = "", durationMs: Int = 6000) {
        if (deviceIp.isBlank()) {
            Log.w(TAG, "No device IP configured, dropping notification")
            return
        }
        Thread {
            try {
                val query = listOf(
                    "app" to app,
                    "title" to title,
                    "message" to message,
                    "file" to file,
                    "duration" to durationMs.toString()
                ).joinToString("&") { (k, v) -> "$k=" + URLEncoder.encode(v, "UTF-8") }

                val url = URL("http://$deviceIp/notify?$query")
                val conn = url.openConnection() as HttpURLConnection
                conn.requestMethod = "GET"
                conn.connectTimeout = 4000
                conn.readTimeout = 4000
                val code = conn.responseCode
                conn.disconnect()
                Log.d(TAG, "Sent notification, response code $code")
            } catch (e: Exception) {
                Log.w(TAG, "Failed to send notification: ${e.message}")
            }
        }.start()
    }
}
