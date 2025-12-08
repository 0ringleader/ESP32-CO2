# ESP32-CO2

**Project:**  
- **Goal:** Build a small CO2 / temperature / humidity monitor with an e-ink display and a web UI. Current version shows readings on the e-ink screen and via a local website. Future versions will add persistent logging (onboard flash or USB drive).

**Hardware used (current):**
- **Controller:** Chinese ESP32-S3 dev kit (note: many S3 dev kits do not provide 5V from the header — check power requirements)  
- **CO2 / Temp / Humidity sensor:** Sensirion SCD41 (SCD4x family)  
- **Display:** WeAct 2.9" three-color e-ink (red/black/white) using `GxEPD2`  
- **Indicator LED:** WS2812 / NeoPixel (single LED used for alert flashing)

**Pinout for the Eink display and CO2 sensor:**

| Weact Eink Module | ESP32-S3 Pin | Description                                     |
| :-------------- | :----------- | :---------------------------------------------- |
| **VCC**         | **3V3**      | Power (3.3V)                                    |
| **GND**         | **GND**      | Ground                                          |
| **SDA**         | **GPIO 11**  | SPI Data (Master Out, Slave In)                 |
| **SCL**         | **GPIO 12**  | SPI Clock                                       |
| **CS**          | **GPIO 10**  | Chip Select                                     |
| **D/C**         | **GPIO 13**  | Data/Command Control                            |
| **RES**         | **GPIO 14**  | Reset                                           |
| **BUSY**        | **GPIO 15**  | Busy Signal (from display to ESP32)             |



| SCD41 Pin | ESP32-S3 Pin | Description |
|-----------|--------------|-------------|
| **VIN**   | **3V3**      | Power (3.3V) |
| **GND**   | **GND**      | Ground |
| **SDA**   | **GPIO 8**   | I2C Data |
| **SCL**   | **GPIO 9**   | I2C Clock |



Notes:
- If your board uses different pins, change the `#define` constants at the top of `website_eInk-RAM_only.ino`.
- The sketch uses software pin numbers directly; these correspond to the GPIO numbers on your ESP32-S3 board.

**Arduino IDE — required libraries**
Install the following via the Arduino Library Manager (or PlatformIO equivalents):
- `GxEPD2` (GxEPD2 e-ink display library)  
- `Adafruit NeoPixel`  
- `SensirionI2cScd4x` (or search for "SCD4x Sensirion" — the library providing `SensirionI2cScd4x.h`)  
- ESP32 core (boards package from Espressif — includes `WiFi.h` and `WebServer.h`)

Tip: In Arduino IDE go to Tools → Manage Libraries… and search for the names above.

**Features**
- Read CO2, temperature, humidity from SCD41 periodically (default: every 10 minutes).
- Display current values on WeAct 2.9" e-ink.
- Web UI with live status and history charts (Chart.js via CDN).  
  - Endpoints: `http://<device-ip>/` (UI), `GET /api/status` (JSON), `GET /api/history` (history JSON), `POST /set-alert` (toggle LED alert), `POST /measure-now` (force measurement)
- History buffer: stores 144 samples (10-min sampling → 24 hours).

**Quick setup steps**
1. Clone this repository or download the sketch folder. Example:
   ```bash
   git clone https://github.com/0ringleader/ESP32-CO2.git
   cd ESP32-CO2/website_eInk-RAM_only
   ```
2. Install ESP32 board support in Arduino IDE:
   - Add the Espressif board manager URL if not added: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools → Board → Boards Manager → search `esp32` → install "esp32 by Espressif Systems".
3. Install the libraries listed above via Library Manager.
4. Open the sketch website_eInk-RAM_only.ino in Arduino IDE.
5. Edit Wi-Fi credentials near the top:
   ```cpp
   const char* ssid = "YOUR_SSID";
   const char* password = "YOUR_PASSWORD";
   ```
6. Confirm the pin mapping. If your dev board's pins differ, update the `#define PIN_*` values at the top of the sketch.
7. Select board: `Tools → Board → ESP32S3 Dev Module` (or matching model), set upload speed & port.
8. Upload the sketch.
9. Open Serial Monitor at `115200` — the sketch prints connection progress and the HTTP server IP address. Visit that IP in a browser to view the web UI.

**Using the Web UI**
- Root page: `http://<ip>/` — shows current CO2/temperature/humidity and 3 charts (CO2, Temperature, Humidity).
- `Force Measurement Now`: triggers an immediate sensor read and refreshes the UI.
- Toggle "Flash LED Alert": enables/disables NeoPixel flashing when CO2 > 2000 ppm.
- API endpoints:
  - `GET /api/status` — returns JSON `{ co2, temp, humidity, status, alert_enabled, alert_threshold }`
  - `GET /api/history` — returns JSON `{ history: [ { co2, temperature, humidity, timestamp }, ... ] }`

**Configuration & constants**
- Sampling interval: change `CHECK_INTERVAL` at the top of the sketch (default 600000 ms = 10 minutes).
- History length: change `MAX_SAMPLES` to adjust how many samples are kept (default 144 = 24 hours at 10-min sampling).
- Alert threshold: change `CO2_ALERT_THRESHOLD` (default 2000 ppm).

**Power & hardware notes / troubleshooting**
- Many cheap ESP32-S3 dev boards do not provide 5V on headers — the e-ink display may need a stable 3.3V/5V supply depending on the model. Check the WeAct display power requirements and feed power appropriately (do not rely on headers if absent).
- If the display doesn't initialize, confirm SPI pins and `GxEPD2` display model match your hardware.
- If the SCD4x doesn't respond, check I2C wiring (SDA/SCL) and the sensor's power rails. Use `i2cdetect`-like scanning if available.
- If Wi-Fi fails to connect in ~30s, check SSID/password and Wi-Fi environment.

**Future plans**
- Add persistent logging to onboard flash or an attached USB drive (option to select storage backend).
- Support multiple board/pin variants and a small config UI to set Wi-Fi and pins without recompiling.
- Add automatic upload of history to cloud or local NAS (optional user-config).

