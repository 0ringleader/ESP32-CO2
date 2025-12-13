# ESP32-S3 CO2 Monitor - Flash & Setup Guide

This version uses a custom partition table to maximize storage for logging and separates web assets (HTML/JS/CSS) into the LittleFS file system.

**Website / Logging Screenshot**

- **Website Logging:** ![Website logging screenshot](website_eink-logging_flash/images/website-logging.png)

_This screenshot shows the web UI with logged data, the download/clear log buttons and chart zoom/pan controls._

## 1. Prerequisites

### Install LittleFS Upload Plugin
To upload the web files (`data/` folder) to the ESP32, you need the LittleFS uploader tool.

1. Follow the install steps here:
https://github.com/earlephilhower/arduino-littlefs-upload?tab=readme-ov-file

2.  Restart Arduino IDE.

## 2. Project Structure

Ensure your folder looks exactly like this:
```
website_eink-logging_flash/
├── website_eink-logging_flash.ino  # Main code
├── partitions.csv                  # Custom partition table
└── data/                           # Web server files
    ├── index.html
    ├── style.css
    └── script.js
```
## 3. Board Configuration (Arduino IDE)

Select **Tools** and configure the following settings for the ESP32-S3 N16R8:

*   **Board:** `ESP32S3 Dev Module`
*   **Flash Size:** `16MB (128Mb)`
*   **Partition Scheme:** `Custom partition table (in sketch)` (Crucial!)
*   **PSRAM:** `OPI PSRAM`
*   **Erase All Flash Before Sketch Upload:** `Enabled` (Only for the very first upload to apply the new partition table, after that turn it off.)

## 4. Flashing Procedure

**Step A: Upload the Firmware (Sketch)**
1.  Connect your ESP32-S3 via USB.
2.  Select the correct **Port**.
3.  Click **Upload** (➡️) or press `Ctrl+U`.
    *   *Note:* Since "Erase All Flash" is enabled, this might take a minute.
    *   *After this step, the device will boot but the website will show "index.html not found". This is normal.*

**Step B: Upload the Web Assets (LittleFS)**
1.  **Important:** If you are on Linux/Mac, you might need to close the Serial Monitor first.
2.  Open the Command Palette: `Ctrl+Shift+P` (or `Cmd+Shift+P` on Mac).
3.  Type and select: `Upload LittleFS to ESP32`.
    *   *Alternatively:* Go to **Tools > ESP32 LittleFS Data Upload**.
4.  Wait for the "LittleFS Image Uploaded" message.

**Step C: Finalize**
1.  Go back to **Tools** and set **Erase All Flash Before Sketch Upload** to `Disabled` (to preserve your logs in the future).
2.  Open the Serial Monitor (`115200` baud) and press the Reset button on the ESP32.
3.  You should see:
    ```
    LittleFS mounted successfully
    Listing files:
      FILE: /index.html ...
      FILE: /style.css ...
      FILE: /script.js ...
    ```

## Features

- **Persistent Logging:** Data is saved to `/log.csv` in flash memory every 10 minutes.
- **Web Interface:** Charts with zoom, pan, and lazy loading of historical data.
- **Management:** Download or Clear logs directly from the web UI.
