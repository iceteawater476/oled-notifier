package com.oledrelay.app

import android.app.Notification
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification

class NotifyListenerService : NotificationListenerService() {

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        super.onNotificationPosted(sbn)

        val packageName = sbn.packageName ?: return
        if (packageName == applicationContext.packageName) return

        val extras = sbn.notification.extras
        val title = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString() ?: ""
        val text = extras.getCharSequence(Notification.EXTRA_TEXT)?.toString() ?: ""
        if (title.isBlank() && text.isBlank()) return

        val deviceIp = Prefs.deviceIp(applicationContext)
        if (deviceIp.isBlank()) return

        val appLabel = try {
            val pm = applicationContext.packageManager
            val info = pm.getApplicationInfo(packageName, 0)
            pm.getApplicationLabel(info).toString()
        } catch (e: Exception) {
            packageName
        }

        val onlyApps = Prefs.onlyApps(applicationContext)
            .split(",").map { it.trim().lowercase() }.filter { it.isNotEmpty() }
        val ignoreApps = Prefs.ignoreApps(applicationContext)
            .split(",").map { it.trim().lowercase() }.filter { it.isNotEmpty() }

        val haystack = (packageName + " " + appLabel).lowercase()
        if (onlyApps.isNotEmpty() && onlyApps.none { haystack.contains(it) }) return
        if (ignoreApps.any { haystack.contains(it) }) return

        NotifySender.send(
            deviceIp = deviceIp,
            app = appLabel,
            title = title,
            message = text,
            durationMs = Prefs.durationMs(applicationContext)
        )
    }
}
