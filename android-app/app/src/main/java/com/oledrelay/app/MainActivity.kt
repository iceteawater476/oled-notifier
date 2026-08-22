package com.oledrelay.app

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.provider.Settings
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast

class MainActivity : Activity() {

    private lateinit var deviceIpInput: EditText
    private lateinit var onlyAppsInput: EditText
    private lateinit var ignoreAppsInput: EditText
    private lateinit var durationInput: EditText
    private lateinit var statusText: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        deviceIpInput = findViewById(R.id.deviceIpInput)
        onlyAppsInput = findViewById(R.id.onlyAppsInput)
        ignoreAppsInput = findViewById(R.id.ignoreAppsInput)
        durationInput = findViewById(R.id.durationInput)
        statusText = findViewById(R.id.statusText)

        deviceIpInput.setText(Prefs.deviceIp(this))
        onlyAppsInput.setText(Prefs.onlyApps(this))
        ignoreAppsInput.setText(Prefs.ignoreApps(this))
        durationInput.setText(Prefs.durationMs(this).toString())

        findViewById<Button>(R.id.saveButton).setOnClickListener {
            val duration = durationInput.text.toString().toIntOrNull() ?: 6000
            Prefs.save(
                this,
                deviceIpInput.text.toString(),
                onlyAppsInput.text.toString(),
                ignoreAppsInput.text.toString(),
                duration
            )
            Toast.makeText(this, "Saved", Toast.LENGTH_SHORT).show()
        }

        findViewById<Button>(R.id.grantAccessButton).setOnClickListener {
            startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
        }

        findViewById<Button>(R.id.testButton).setOnClickListener {
            val deviceIp = deviceIpInput.text.toString()
            if (deviceIp.isBlank()) {
                Toast.makeText(this, "Enter a device IP first", Toast.LENGTH_SHORT).show()
            } else {
                NotifySender.send(
                    deviceIp = deviceIp,
                    app = "OLED Notify Relay",
                    title = "Test notification",
                    message = "If you see this on the OLED, it's working!"
                )
                Toast.makeText(this, "Test sent", Toast.LENGTH_SHORT).show()
            }
        }
    }

    override fun onResume() {
        super.onResume()
        statusText.text = if (isNotificationAccessGranted()) {
            "Notification access: granted"
        } else {
            "Notification access: NOT granted -- tap the button below and enable it for this app"
        }
    }

    private fun isNotificationAccessGranted(): Boolean {
        val flat = Settings.Secure.getString(contentResolver, "enabled_notification_listeners") ?: return false
        return flat.contains(packageName)
    }
}
