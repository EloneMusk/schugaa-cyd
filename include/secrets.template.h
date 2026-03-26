#ifndef SECRETS_H
#define SECRETS_H

// Wi-Fi network used by the ESP32 CYD
#define WIFI_SSID     "YOUR_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// LibreLinkUp account used for direct login from the device
#define LIBRE_EMAIL      "your-email@example.com"
#define LIBRE_PASSWORD   "your-libre-password"
#define LIBRE_REGION     "DE"

// Optional: set this only if the Libre account follows multiple patients
#define LIBRE_PATIENT_ID ""

// Time zone used for automatic light/dark theme switching
#define UI_TIMEZONE_TZ   "CET-1CEST,M3.5.0/2,M10.5.0/3"

#endif // SECRETS_H
