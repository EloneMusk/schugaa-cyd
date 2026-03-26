# Schugaa CYD

`Schugaa CYD` is a standalone blood sugar display for the ESP32 Cheap Yellow Display (`ESP32-2432S028R`).  
The device connects to Wi-Fi, logs into LibreLinkUp directly from the ESP32, fetches current glucose data plus graph history, and renders it locally on the touchscreen display.

## What It Does

- Shows current glucose value and trend
- Draws a glucose history graph with range coloring
- Fetches LibreLinkUp data directly on the ESP32
- Supports automatic light/dark mode based on time of day
- Supports manual theme toggle by touching the screen

No Python backend, Raspberry Pi, or always-on computer is required.

## Hardware

- ESP32 Cheap Yellow Display (`ESP32-2432S028R`)
- USB cable for flashing
- Wi-Fi connection
- LibreLinkUp account with follower access to the sensor data

## Project Structure

- [`src/main.cpp`](/Users/abhishek/Downloads/schugaa-cyd/src/main.cpp)  
  Main firmware: Wi-Fi, LibreLinkUp login, data fetching, rendering, and touch handling
- [`include/secrets.template.h`](/Users/abhishek/Downloads/schugaa-cyd/include/secrets.template.h)  
  Safe template you can commit to Git
- [`include/secrets.h`](/Users/abhishek/Downloads/schugaa-cyd/include/secrets.h)  
  Local credentials file used for building on your machine
- [`platformio.ini`](/Users/abhishek/Downloads/schugaa-cyd/platformio.ini)  
  PlatformIO board and library configuration

## Setup

1. Copy [`include/secrets.template.h`](/Users/abhishek/Downloads/schugaa-cyd/include/secrets.template.h) to `include/secrets.h`.
2. Fill in your Wi-Fi and LibreLinkUp credentials in `include/secrets.h`.
3. Build and flash the firmware.

`include/secrets.h` is ignored by Git, so your real credentials stay local.

## Build With VS Code

1. Install [Visual Studio Code](https://code.visualstudio.com/).
2. Install the PlatformIO extension.
3. Open this project folder in VS Code.
4. Make sure `include/secrets.h` exists and contains your real values.
5. Use PlatformIO:
   - `Build`
   - `Upload`
   - `Monitor`

Useful serial speed:

```text
115200
```

## Build Without VS Code

You can use PlatformIO Core from the terminal.

1. Install PlatformIO Core if needed:

```bash
python3 -m pip install -U platformio
```

2. From the project root, build:

```bash
platformio run
```

3. Upload to the device:

```bash
platformio run -t upload
```

4. Open the serial monitor:

```bash
platformio device monitor -b 115200
```

5. If you want a clean rebuild:

```bash
platformio run -t clean
platformio run
```

## Notes

- The firmware fetches data every 60 seconds.
- The project uses direct LibreLinkUp HTTPS calls from the ESP32.
- The touch screen can toggle between light and dark mode.
- Automatic theme switching uses the time zone from `UI_TIMEZONE_TZ`.

## Git Safety

Files now ignored from Git include:

- local secrets file
- Python cache files
- local cache artifacts
- local `.env` leftovers

The file intended for Git is [`include/secrets.template.h`](/Users/abhishek/Downloads/schugaa-cyd/include/secrets.template.h).
