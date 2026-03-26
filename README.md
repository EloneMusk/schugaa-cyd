# Schugaa CYD

`Schugaa CYD` is a standalone blood sugar display for the ESP32 Cheap Yellow Display (`ESP32-2432S028R`).  
It connects to LibreLinkUp directly from the ESP32 and renders the glucose graph on-device.

## What This Project Does

- Connects to Wi-Fi and fetches glucose data from LibreLinkUp
- Draws current value, trend, history graph, and status info on CYD
- Runs an always-on configuration hotspot + web portal so credentials can be changed anytime
- Supports automatic light/dark mode (time-based) and manual touch toggle

## New Login Flow (No Reflash Needed for Credential Changes)

On every boot, the device starts a hotspot and keeps it running:

- AP SSID: from `CONFIG_AP_SSID` in [`include/secrets.h`](/Users/abhishek/Downloads/schugaa-cyd/include/secrets.h)
- AP password: from `CONFIG_AP_PASSWORD` in [`include/secrets.h`](/Users/abhishek/Downloads/schugaa-cyd/include/secrets.h)
- Portal URL: `http://192.168.4.1`

From the portal, user can:

- Enter Libre email
- Enter Libre password
- Select region from all supported regions (dropdown)
- Optionally set patient ID
- Click **Login & Save** to validate credentials and store them

Success/error messages are shown on the same page.

Credentials are saved in ESP32 persistent storage (NVS), not in source files.

## About `secrets.h` Saving

An ESP32 firmware cannot safely rewrite your local source file `include/secrets.h` on your computer after flashing.  
Instead, the portal stores credentials in NVS and provides a **generated secrets header download** at:

- `http://192.168.4.1/secrets.h`

This keeps runtime config easy while still letting you export a header snippet when needed.

## Project Structure

- [`src/main.cpp`](/Users/abhishek/Downloads/schugaa-cyd/src/main.cpp)  
  Main firmware (Wi-Fi, AP portal, Libre auth/data fetch, UI rendering, touch/theme)
- [`include/secrets.template.h`](/Users/abhishek/Downloads/schugaa-cyd/include/secrets.template.h)  
  Safe template to commit
- [`include/secrets.h`](/Users/abhishek/Downloads/schugaa-cyd/include/secrets.h)  
  Local build-time defaults and AP settings (ignored by git)
- [`platformio.ini`](/Users/abhishek/Downloads/schugaa-cyd/platformio.ini)  
  PlatformIO project config

## Setup

1. Copy [`include/secrets.template.h`](/Users/abhishek/Downloads/schugaa-cyd/include/secrets.template.h) to `include/secrets.h`.
2. Set at least:
   - `WIFI_SSID`
   - `WIFI_PASSWORD`
   - `CONFIG_AP_SSID`
   - `CONFIG_AP_PASSWORD`
3. Build and flash.
4. Open portal at `192.168.4.1` on the hotspot to set Libre credentials.

## Build With VS Code

1. Install VS Code + PlatformIO extension.
2. Open this folder in VS Code.
3. Click PlatformIO `Build`, `Upload`, and `Monitor`.
4. Use serial baud `115200`.

## Build Without VS Code (CLI)

```bash
platformio run
platformio run -t upload
platformio device monitor -b 115200
```

Clean rebuild:

```bash
platformio run -t clean
platformio run
```

## Git Safety

`include/secrets.h` is ignored by git. Commit only [`include/secrets.template.h`](/Users/abhishek/Downloads/schugaa-cyd/include/secrets.template.h).
