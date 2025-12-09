#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Wire.h>
#include <SensirionI2cScd4x.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <LittleFS.h>

// --- WIFI CONFIG ---
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// --- WEBSERVER & LED CONFIG ---
WebServer server(80);
#define LED_PIN 48
#define LED_COUNT 1
Adafruit_NeoPixel pixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

bool flash_alert_enabled = true;
#define CO2_ALERT_THRESHOLD 2000

// --- DATA LOGGING CONFIG ---
#define CHECK_INTERVAL 600000  // 10 minutes
#define LOG_FILE "/log.csv"
#define MAX_LOG_SIZE (10 * 1024 * 1024)  // 10 MB max log size, then rotate

// RAM buffer for recent data (for charts - last 24h)
#define MAX_SAMPLES 144
struct SensorData {
  uint16_t co2;
  float temperature;
  float humidity;
  long timestamp;
};
SensorData history_data[MAX_SAMPLES];
int history_index = 0;
bool is_history_full = false;

// --- E-INK & SCD41 CONFIG ---
#define PIN_EINK_SDA    11
#define PIN_EINK_SCL    12
#define PIN_EINK_CS     10
#define PIN_EINK_DC     13
#define PIN_EINK_RST    14
#define PIN_EINK_BUSY   15

#define PIN_I2C_SDA     8
#define PIN_I2C_SCL     9

#define TEMP_THRESHOLD 2.0
#define CO2_THRESHOLD 100
#define HUMIDITY_THRESHOLD 5.0

GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT> display(GxEPD2_290_C90c(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RST, PIN_EINK_BUSY));
SensirionI2cScd4x scd4x;

// Timing & state
unsigned long lastCheck = 0;
unsigned long lastFlashToggle = 0;
bool firstRun = true;
bool is_flashing = false;

// Current values
uint16_t current_co2 = 0;
float current_temperature = 0.0f;
float current_humidity = 0.0f;

// Reference values for e-ink threshold updates
float refTemperature = 0.0f;
uint16_t refCO2 = 0;
float refHumidity = 0.0f;

// Function prototypes
void handleRoot();
void handleStyle();
void handleScript();
void handleSetAlert();
void handleStatusApi();
void handleHistoryApi();
void handleDownloadLog();
void handleClearLog();
void handleMeasureNow();
void init_wifi();
void init_time();
void init_littlefs();
void log_data_to_flash(uint16_t co2, float temperature, float humidity);
void log_data_to_ram(uint16_t co2, float temperature, float humidity);
void load_recent_history();
void update_led_state();
String generateHistoryJson();
String generateHistoryFromFlash(int page, int pageSize);
const char* getCO2Status(uint16_t co2);
int getCO2Color(uint16_t co2);
void showStartupScreen();
void updateDisplay(uint16_t co2, float temperature, float humidity);
void showErrorScreen();
void checkAndUpdateIfNeeded();

// *** SETUP ***
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32-S3 CO2 Monitor (LittleFS Logging) ===");

  // Initialize NeoPixel LED
  pixel.begin();
  pixel.setBrightness(50);
  pixel.show();

  // Initialize LittleFS
  init_littlefs();

  // Initialize I2C for SCD41
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  scd4x.begin(Wire, SCD41_I2C_ADDR_62);

  scd4x.stopPeriodicMeasurement();
  if (scd4x.startPeriodicMeasurement() == 0) {
    Serial.println("SCD41 started successfully");
  } else {
    Serial.println("Error starting SCD41");
  }

  // Initialize SPI & Display
  SPI.begin(PIN_EINK_SCL, -1, PIN_EINK_SDA, PIN_EINK_CS);
  display.init(115200);
  display.setRotation(1);
  Serial.println("Display initialized");

  showStartupScreen();

  // Initialize Wi-Fi and Time
  init_wifi();
  init_time();

  // Load recent history from CSV into RAM buffer
  load_recent_history();

  // Web Server Routing
  server.on("/", HTTP_GET, handleRoot);
  server.on("/style.css", HTTP_GET, handleStyle);
  server.on("/script.js", HTTP_GET, handleScript);
  server.on("/set-alert", HTTP_POST, handleSetAlert);
  server.on("/api/status", HTTP_GET, handleStatusApi);
  server.on("/api/history", HTTP_GET, handleHistoryApi);
  server.on("/download-log", HTTP_GET, handleDownloadLog);
  server.on("/clear-log", HTTP_POST, handleClearLog);
  server.on("/measure-now", HTTP_POST, handleMeasureNow);
  server.begin();
  Serial.printf("HTTP server started at http://%s/\n", WiFi.localIP().toString().c_str());

  Serial.println("Waiting for first sensor reading...");
  delay(5000);
}

// *** LOOP ***
void loop() {
  unsigned long currentMillis = millis();

  if (firstRun || (currentMillis - lastCheck >= CHECK_INTERVAL)) {
    checkAndUpdateIfNeeded();
    lastCheck = currentMillis;
    firstRun = false;
  }

  server.handleClient();
  update_led_state();
  delay(100);
}

// *** LittleFS Initialization ***
void init_littlefs() {
  if (!LittleFS.begin(true)) {  // true = format if mount fails
    Serial.println("LittleFS mount failed!");
    return;
  }
  Serial.println("LittleFS mounted successfully");

  // Print filesystem info
  size_t totalBytes = LittleFS.totalBytes();
  size_t usedBytes = LittleFS.usedBytes();
  Serial.printf("LittleFS: Total: %u bytes, Used: %u bytes, Free: %u bytes\n",
                totalBytes, usedBytes, totalBytes - usedBytes);

  // Create log file with header if it doesn't exist
  if (!LittleFS.exists(LOG_FILE)) {
    File f = LittleFS.open(LOG_FILE, FILE_WRITE);
    if (f) {
      f.println("timestamp,co2,temperature,humidity");
      f.close();
      Serial.println("Created new log file with header");
    }
  }
}

// *** Log data to flash CSV ***
void log_data_to_flash(uint16_t co2, float temperature, float humidity) {
  // Check file size and rotate if needed
  File f = LittleFS.open(LOG_FILE, FILE_READ);
  if (f) {
    size_t fileSize = f.size();
    f.close();
    if (fileSize > MAX_LOG_SIZE) {
      // Rotate: rename old, create new
      LittleFS.remove("/log_old.csv");
      LittleFS.rename(LOG_FILE, "/log_old.csv");
      File newFile = LittleFS.open(LOG_FILE, FILE_WRITE);
      if (newFile) {
        newFile.println("timestamp,co2,temperature,humidity");
        newFile.close();
      }
      Serial.println("Log rotated due to size limit");
    }
  }

  // Append data
  f = LittleFS.open(LOG_FILE, FILE_APPEND);
  if (!f) {
    Serial.println("Failed to open log file for append");
    return;
  }

  time_t now = time(nullptr);
  f.printf("%ld,%u,%.2f,%.2f\n", now, co2, temperature, humidity);
  f.close();
  Serial.printf("Logged to flash: %ld,%u,%.2f,%.2f\n", now, co2, temperature, humidity);
}

// *** Log data to RAM buffer (for charts) ***
void log_data_to_ram(uint16_t co2, float temperature, float humidity) {
  time_t now = time(nullptr);
  SensorData new_data = {co2, temperature, humidity, now};

  history_data[history_index] = new_data;
  history_index++;

  if (history_index >= MAX_SAMPLES) {
    history_index = 0;
    is_history_full = true;
  }
}

// *** Load recent history from CSV into RAM ***
void load_recent_history() {
  File f = LittleFS.open(LOG_FILE, FILE_READ);
  if (!f) {
    Serial.println("No log file to load history from");
    return;
  }

  // Skip header
  f.readStringUntil('\n');

  // Count lines first
  int lineCount = 0;
  while (f.available()) {
    f.readStringUntil('\n');
    lineCount++;
  }

  // Go back and read last MAX_SAMPLES lines
  f.seek(0);
  f.readStringUntil('\n');  // skip header again

  int skipLines = (lineCount > MAX_SAMPLES) ? (lineCount - MAX_SAMPLES) : 0;
  for (int i = 0; i < skipLines && f.available(); i++) {
    f.readStringUntil('\n');
  }

  // Read remaining lines into buffer
  history_index = 0;
  is_history_full = false;

  while (f.available() && history_index < MAX_SAMPLES) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    // Parse: timestamp,co2,temperature,humidity
    int idx1 = line.indexOf(',');
    int idx2 = line.indexOf(',', idx1 + 1);
    int idx3 = line.indexOf(',', idx2 + 1);

    if (idx1 > 0 && idx2 > idx1 && idx3 > idx2) {
      history_data[history_index].timestamp = line.substring(0, idx1).toInt();
      history_data[history_index].co2 = line.substring(idx1 + 1, idx2).toInt();
      history_data[history_index].temperature = line.substring(idx2 + 1, idx3).toFloat();
      history_data[history_index].humidity = line.substring(idx3 + 1).toFloat();
      history_index++;
    }
  }

  f.close();
  Serial.printf("Loaded %d history entries from flash\n", history_index);

  if (history_index >= MAX_SAMPLES) {
    history_index = 0;
    is_history_full = true;
  }
}

// *** LED State ***
void update_led_state() {
  if (flash_alert_enabled && current_co2 > CO2_ALERT_THRESHOLD) {
    if (millis() - lastFlashToggle >= 500) {
      is_flashing = !is_flashing;
      pixel.setPixelColor(0, is_flashing ? 0xFF0000 : 0x000000);
      pixel.show();
      lastFlashToggle = millis();
    }
  } else {
    pixel.setPixelColor(0, 0x000000);
    pixel.show();
    is_flashing = false;
  }
}

// *** Wi-Fi ***
void init_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  uint32_t start_time = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    float pulse = (sin(millis() / 500.0) + 1.0) / 2.0;
    pixel.setPixelColor(0, pixel.Color(0, 0, (uint8_t)(pulse * 100)));
    pixel.show();
    if (millis() - start_time > 30000) {
      Serial.println("\nConnection Timeout!");
      break;
    }
  }
  pixel.setPixelColor(0, 0x000000);
  pixel.show();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected.");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi.");
  }
}

// *** NTP Time ***
void init_time() {
  configTime(3600, 3600, "pool.ntp.org");
  Serial.println("Waiting for NTP time synchronization...");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    now = time(nullptr);
  }
  Serial.printf("Time synchronized. Current time: %s", ctime(&now));
}

// *** Check and Update ***
void checkAndUpdateIfNeeded() {
  uint16_t co2 = 0;
  float temperature = 0.0f;
  float humidity = 0.0f;
  uint16_t error = scd4x.readMeasurement(co2, temperature, humidity);

  if (error || co2 == 0) {
    Serial.print("Error reading sensor: ");
    Serial.println(error ? String(error) : "CO2=0");
    showErrorScreen();
    return;
  }

  current_co2 = co2;
  current_temperature = temperature;
  current_humidity = humidity;

  // Log to both flash and RAM
  log_data_to_flash(co2, temperature, humidity);
  log_data_to_ram(co2, temperature, humidity);

  Serial.println("\n--- Sensor Check ---");
  Serial.printf("Current - CO2: %d ppm, Temp: %.2f°C, Humidity: %.2f%%\n",
                co2, temperature, humidity);

  if (refCO2 == 0) {
    Serial.println("First reading - initializing reference values");
    refCO2 = co2;
    refTemperature = temperature;
    refHumidity = humidity;
    updateDisplay(co2, temperature, humidity);
    return;
  }

  float tempDelta = abs(temperature - refTemperature);
  int co2Delta = abs((int)co2 - (int)refCO2);
  float humidityDelta = abs(humidity - refHumidity);

  if (tempDelta >= TEMP_THRESHOLD || co2Delta >= CO2_THRESHOLD || humidityDelta >= HUMIDITY_THRESHOLD) {
    Serial.println(">>> THRESHOLD EXCEEDED - Updating display <<<");
    refCO2 = co2;
    refTemperature = temperature;
    refHumidity = humidity;
    updateDisplay(co2, temperature, humidity);
  } else {
    Serial.println("No threshold exceeded - display not updated");
  }
}

// *** WEB HANDLERS ***

void handleRoot() {
  File f = LittleFS.open("/index.html", FILE_READ);
  if (!f) {
    server.send(500, "text/plain", "index.html not found on LittleFS");
    return;
  }
  server.streamFile(f, "text/html");
  f.close();
}

void handleStyle() {
  File f = LittleFS.open("/style.css", FILE_READ);
  if (!f) {
    server.send(404, "text/plain", "style.css not found");
    return;
  }
  server.streamFile(f, "text/css");
  f.close();
}

void handleScript() {
  File f = LittleFS.open("/script.js", FILE_READ);
  if (!f) {
    server.send(404, "text/plain", "script.js not found");
    return;
  }
  server.streamFile(f, "application/javascript");
  f.close();
}

void handleSetAlert() {
  if (server.hasArg("enabled")) {
    flash_alert_enabled = (server.arg("enabled") == "true");
    Serial.printf("Alert set to: %s\n", flash_alert_enabled ? "Enabled" : "Disabled");
    server.send(200, "text/plain", flash_alert_enabled ? "Alert enabled" : "Alert disabled");
  } else {
    server.send(400, "text/plain", "Missing parameter 'enabled'");
  }
}

void handleStatusApi() {
  String json = "{";
  json += "\"co2\":" + String(current_co2) + ",";
  json += "\"temp\":" + String(current_temperature, 2) + ",";
  json += "\"humidity\":" + String(current_humidity, 2) + ",";
  json += "\"status\":\"" + String(getCO2Status(current_co2)) + "\",";
  json += "\"alert_enabled\":" + String(flash_alert_enabled ? "true" : "false") + ",";
  json += "\"alert_threshold\":" + String(CO2_ALERT_THRESHOLD);
  json += "}";
  server.send(200, "application/json", json);
}

void handleHistoryApi() {
  String json = "{\"history\":[";
  json += generateHistoryJson();
  json += "]}";
  server.send(200, "application/json", json);
}

// Update handleHistoryApi to support pagination
void handleHistoryApi() {
  int page = 0;
  int pageSize = MAX_SAMPLES;
  
  if (server.hasArg("page")) {
    page = server.arg("page").toInt();
  }
  if (server.hasArg("size")) {
    pageSize = server.arg("size").toInt();
    if (pageSize > 500) pageSize = 500;  // Limit max page size
    if (pageSize < 10) pageSize = 10;
  }

  String json = "{\"history\":[";
  
  if (page == 0) {
    // Page 0: return RAM buffer (most recent data)
    json += generateHistoryJson();
    json += "],\"hasMore\":true,\"page\":0}";
  } else {
    // Page > 0: read older data from flash CSV
    json += generateHistoryFromFlash(page, pageSize);
    json += "]}";
  }
  
  server.send(200, "application/json", json);
}

// New function to read paginated history from flash
String generateHistoryFromFlash(int page, int pageSize) {
  File f = LittleFS.open(LOG_FILE, FILE_READ);
  if (!f) {
    return "],\"hasMore\":false,\"page\":" + String(page);
  }

  // Skip header
  f.readStringUntil('\n');

  // Count total lines
  int totalLines = 0;
  while (f.available()) {
    f.readStringUntil('\n');
    totalLines++;
  }

  // Calculate offset for this page
  // Page 1 = data before the RAM buffer (entries 0 to totalLines - MAX_SAMPLES - 1)
  // We read backwards from the end of file
  int ramBufferStart = max(0, totalLines - MAX_SAMPLES);
  int pageStart = ramBufferStart - (page * pageSize);
  int pageEnd = pageStart + pageSize;
  
  if (pageStart < 0) {
    // No more data
    f.close();
    return "],\"hasMore\":false,\"page\":" + String(page);
  }
  
  pageStart = max(0, pageStart);
  pageEnd = min(pageEnd, ramBufferStart);

  // Seek back to beginning and skip to our page
  f.seek(0);
  f.readStringUntil('\n');  // skip header
  
  for (int i = 0; i < pageStart && f.available(); i++) {
    f.readStringUntil('\n');
  }

  // Read the page data
  String data = "";
  int count = 0;
  while (f.available() && count < (pageEnd - pageStart)) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    int idx1 = line.indexOf(',');
    int idx2 = line.indexOf(',', idx1 + 1);
    int idx3 = line.indexOf(',', idx2 + 1);

    if (idx1 > 0 && idx2 > idx1 && idx3 > idx2) {
      if (data.length() > 0) data += ",";
      data += "{\"timestamp\":" + line.substring(0, idx1) + ",";
      data += "\"co2\":" + line.substring(idx1 + 1, idx2) + ",";
      data += "\"temperature\":" + line.substring(idx2 + 1, idx3) + ",";
      data += "\"humidity\":" + line.substring(idx3 + 1) + "}";
      count++;
    }
  }

  f.close();
  
  bool hasMore = (pageStart > 0);
  return data + "],\"hasMore\":" + String(hasMore ? "true" : "false") + ",\"page\":" + String(page);
}

void handleDownloadLog() {
  File f = LittleFS.open(LOG_FILE, FILE_READ);
  if (!f) {
    server.send(404, "text/plain", "Log file not found");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=co2_log.csv");
  server.streamFile(f, "text/csv");
  f.close();
}

void handleClearLog() {
  LittleFS.remove(LOG_FILE);
  File f = LittleFS.open(LOG_FILE, FILE_WRITE);
  if (f) {
    f.println("timestamp,co2,temperature,humidity");
    f.close();
  }
  // Clear RAM buffer too
  history_index = 0;
  is_history_full = false;
  server.send(200, "text/plain", "Log cleared");
  Serial.println("Log file cleared");
}

void handleMeasureNow() {
  Serial.println("Manual measurement requested...");
  lastCheck = millis();
  checkAndUpdateIfNeeded();
  server.send(200, "text/plain", "Measurement completed");
}

String generateHistoryJson() {
  String data = "";
  int start_index = is_history_full ? history_index : 0;
  int count = is_history_full ? MAX_SAMPLES : history_index;

  for (int i = 0; i < count; i++) {
    int idx = (start_index + i) % MAX_SAMPLES;
    if (history_data[idx].co2 > 0) {
      if (data.length() > 0) data += ",";
      data += "{\"co2\":" + String(history_data[idx].co2) + "," +
              "\"temperature\":" + String(history_data[idx].temperature, 2) + "," +
              "\"humidity\":" + String(history_data[idx].humidity, 2) + "," +
              "\"timestamp\":" + String(history_data[idx].timestamp) + "}";
    }
  }
  return data;
}

// *** E-INK FUNCTIONS ***

void showStartupScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold18pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(20, 40);
    display.print("CO2 Monitor");

    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_RED);
    display.setCursor(30, 70);
    display.print("Initializing...");

    display.setTextColor(GxEPD_BLACK);
    display.setCursor(20, 100);
    display.print("LittleFS Logging");
  } while (display.nextPage());
}

void updateDisplay(uint16_t co2, float temperature, float humidity) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.fillRect(0, 0, display.width(), 30, GxEPD_BLACK);
    display.setFont(&FreeMonoBold12pt7b);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(60, 21);
    display.print("CO2 Monitor");

    display.setFont(&FreeMonoBold18pt7b);
    int co2Color = getCO2Color(co2);
    display.setTextColor(co2Color);
    display.setCursor(20, 65);
    display.printf("%d ppm", co2);

    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(co2Color);
    display.setCursor(20, 85);
    display.print(getCO2Status(co2));

    int barWidth = map(min((int)co2, 2000), 400, 2000, 10, display.width() - 20);
    display.fillRect(10, 95, barWidth, 8, co2Color);
    display.drawRect(10, 95, display.width() - 20, 8, GxEPD_BLACK);

    display.setFont(&FreeMonoBold12pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(20, 120);
    display.printf("%.1fC", temperature);
    display.setCursor(170, 120);
    display.printf("%.1f%%", humidity);
  } while (display.nextPage());

  Serial.println("Display updated successfully");
}

void showErrorScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold12pt7b);
    display.setTextColor(GxEPD_RED);
    display.setCursor(50, 60);
    display.print("Sensor Error");

    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(30, 90);
    display.print("Check wiring");
  } while (display.nextPage());
}

int getCO2Color(uint16_t co2) {
  return (co2 < 1200) ? GxEPD_BLACK : GxEPD_RED;
}

const char* getCO2Status(uint16_t co2) {
  if (co2 < 800) return "Good";
  if (co2 < 1200) return "Moderate";
  if (co2 < 1500) return "High";
  return "Very High";
}