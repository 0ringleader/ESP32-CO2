#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Wire.h>
#include <SensirionI2cScd4x.h>

// *** NEW LIBRARY FOR LED ***
#include <Adafruit_NeoPixel.h>
// *** NEW INCLUDES FOR WEBSERVER ***
#include <WiFi.h>
#include <WebServer.h>
#include <time.h> // For NTP time synchronization

// --- WIFI CONFIG ---
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// --- WEBSERVER & LED CONFIG ---
WebServer server(80);
// ** MODIFIED LED PIN & TYPE **
#define LED_PIN 48 // User-specified NeoPixel pin
#define LED_COUNT 1 // We are controlling only one LED
Adafruit_NeoPixel pixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

bool flash_alert_enabled = true; // State of the toggle switch
#define CO2_ALERT_THRESHOLD 2000 // ppm for flashing

// --- DATA LOGGING CONFIG ---
// Check sensor every 10 minutes (600,000 ms)
#define CHECK_INTERVAL 600000
// Store 24 hours of data (24 hours * 60 min/hr / 10 min/sample = 144 samples)
#define MAX_SAMPLES 144

struct CO2_Data {
  uint16_t co2;
  float temperature;
  float humidity;
  long timestamp; // Unix timestamp for graphing
};

CO2_Data history_data[MAX_SAMPLES];
int history_index = 0;
bool is_history_full = false;

// --- E-INK & SCD41 CONFIG (from original code) ---
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

// Timing
unsigned long lastCheck = 0;
unsigned long lastFlashToggle = 0;
bool firstRun = true;
bool is_flashing = false; // State of the flashing LED

// Current values (for webserver status)
uint16_t current_co2 = 0;
float current_temperature = 0.0f;
float current_humidity = 0.0f;

// Reference values (updated only when threshold crossed)
float refTemperature = 0.0f;
uint16_t refCO2 = 0;
float refHumidity = 0.0f;

// *** FUNCTION PROTOTYPES ***
void handleRoot();
void handleSetAlert();
void handleStatusApi();
void handleHistoryApi();
void init_wifi();
void init_time();
void log_data(uint16_t co2, float temperature, float humidity);
void update_led_state(); // Controls the NeoPixel
String generateHtmlPage();
String generateChartData();
// ** CORRECTED PROTOTYPE RETURN TYPE **
const char* getCO2Status(uint16_t co2);
int getCO2Color(uint16_t co2);
void showStartupScreen();
void updateDisplay(uint16_t co2, float temperature, float humidity);
void showErrorScreen();
void handleMeasureNow(); // <-- ADD THIS

// *** SETUP ***
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32-S3 CO2 Monitor (Web & Threshold) ===");

  // Initialize NeoPixel LED
  pixel.begin();
  pixel.setBrightness(50); // Set a reasonable brightness
  pixel.show(); // Initialize all pixels to 'off'

  // Initialize I2C for SCD41
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  scd4x.begin(Wire, SCD41_I2C_ADDR_62);

  // Stop & Start periodic measurement
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

  // Web Server Routing
  server.on("/", HTTP_GET, handleRoot);
  server.on("/set-alert", HTTP_POST, handleSetAlert);
  server.on("/api/status", HTTP_GET, handleStatusApi);
  server.on("/api/history", HTTP_GET, handleHistoryApi);
  server.on("/measure-now", HTTP_POST, handleMeasureNow);
  server.begin();
  Serial.printf("HTTP server started at http://%s/\n", WiFi.localIP().toString().c_str());

  // Wait for first sensor reading
  Serial.println("Waiting for first sensor reading...");
  delay(5000);
}

// *** LOOP ***
void loop() {
  unsigned long currentMillis = millis();

  // 1. Sensor Check and Update (E-ink & Logging)
  if (firstRun || (currentMillis - lastCheck >= CHECK_INTERVAL)) {
    checkAndUpdateIfNeeded();
    lastCheck = currentMillis;
    firstRun = false;
  }

  // 2. Web Server Handling
  server.handleClient();

  // 3. LED Alert Flashing
  update_led_state();

  delay(100);
}

// *** MODIFIED FUNCTION: Update LED State (Flashing/Off) ***
void update_led_state() {
  if (flash_alert_enabled && current_co2 > CO2_ALERT_THRESHOLD) {
    // Flashing logic (Red color: 0xFF0000)
    if (millis() - lastFlashToggle >= 500) { // Toggle every 500ms
      is_flashing = !is_flashing;
      if (is_flashing) {
        pixel.setPixelColor(0, 0xFF0000); // Red
      } else {
        pixel.setPixelColor(0, 0x000000); // Off
      }
      pixel.show();
      lastFlashToggle = millis();
    }
  } else {
    // LED off
    pixel.setPixelColor(0, 0x000000); // Off
    pixel.show();
    is_flashing = false;
  }
}

// *** REST OF THE CODE (No changes needed for the rest of the functions) ***

// *** NEW FUNCTION: Initialize Wi-Fi ***
void init_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  // Use NeoPixel to show connection status (e.g., pulsing blue)
  uint32_t start_time = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    // Flash NeoPixel during connection attempt
    float pulse = (sin(millis() / 500.0) + 1.0) / 2.0; // 0.0 to 1.0
    pixel.setPixelColor(0, pixel.Color(0, 0, (uint8_t)(pulse * 100))); // Blue pulse
    pixel.show();
    if (millis() - start_time > 30000) { // Timeout after 30 seconds
        Serial.println("\nConnection Timeout!");
        break;
    }
  }
  pixel.setPixelColor(0, 0x000000); // Turn LED off once connected
  pixel.show();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected.");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi.");
  }
}

// *** NEW FUNCTION: Initialize NTP Time ***
void init_time() {
  configTime(3600, 3600, "pool.ntp.org"); // CET (Central European Time) configuration
  Serial.println("Waiting for NTP time synchronization...");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) { // Wait until time is set
    delay(500);
    now = time(nullptr);
  }
  Serial.printf("Time synchronized. Current time: %s", ctime(&now));
}

// *** MODIFIED FUNCTION: Check and Update ***
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

  // Update global current values
  current_co2 = co2;
  current_temperature = temperature;
  current_humidity = humidity;

  // Log data to history
  log_data(co2, temperature, humidity);

  // Print current readings
  Serial.println("\n--- Sensor Check ---");
  Serial.printf("Current - CO2: %d ppm, Temp: %.2f°C, Humidity: %.2f%%\n",
                co2, temperature, humidity);

  // Initialize reference values on first run
  if (refCO2 == 0) {
    Serial.println("First reading - initializing reference values and updating display");
    refCO2 = co2;
    refTemperature = temperature;
    refHumidity = humidity;
    updateDisplay(co2, temperature, humidity);
    return;
  }

  // Check if any threshold exceeded (for E-ink update)
  float tempDelta = abs(temperature - refTemperature);
  int co2Delta = abs((int)co2 - (int)refCO2);
  float humidityDelta = abs(humidity - refHumidity);

  bool tempExceeded = tempDelta >= TEMP_THRESHOLD;
  bool co2Exceeded = co2Delta >= CO2_THRESHOLD;
  bool humidityExceeded = humidityDelta >= HUMIDITY_THRESHOLD;

  if (tempExceeded || co2Exceeded || humidityExceeded) {
    Serial.println(">>> THRESHOLD EXCEEDED - Updating display <<<");
    refCO2 = co2;
    refTemperature = temperature;
    refHumidity = humidity;
    updateDisplay(co2, temperature, humidity);
  } else {
    Serial.println("No threshold exceeded - display not updated");
  }
}

// *** MODIFIED FUNCTION: Log Sensor Data ***
void log_data(uint16_t co2, float temperature, float humidity) {
  time_t now = time(nullptr);
  CO2_Data new_data = {co2, temperature, humidity, now};

  history_data[history_index] = new_data;
  history_index++;

  if (history_index >= MAX_SAMPLES) {
    history_index = 0; // Wrap around
    is_history_full = true;
    Serial.println("History buffer full. Wrapping around.");
  }
  Serial.printf("Data logged. Sample index: %d\n", history_index);
}

// *** NEW WEBSERVER HANDLERS ***

// Main HTML page
void handleRoot() {
  server.send(200, "text/html", generateHtmlPage());
}

// Set Alert Toggle API
void handleSetAlert() {
  if (server.hasArg("enabled")) {
    String enabled = server.arg("enabled");
    flash_alert_enabled = (enabled == "true");
    Serial.printf("Alert flashing set to: %s\n", flash_alert_enabled ? "Enabled" : "Disabled");
    server.send(200, "text/plain", flash_alert_enabled ? "Alert enabled" : "Alert disabled");
  } else {
    server.send(400, "text/plain", "Missing parameter 'enabled'");
  }
}

// Current Status API
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

// History Data API
void handleHistoryApi() {
  String json = "{\"history\":[";
  json += generateChartData();
  json += "]}";
  server.send(200, "application/json", json);
}

// *** NEW WEBSERVER HANDLER: Force immediate measurement ***
void handleMeasureNow() {
  Serial.println("Manual measurement requested...");
  // Set lastCheck back to 0 to force an immediate run in the next loop iteration,
  // but we call the function directly for instant response.
  lastCheck = millis(); 
  checkAndUpdateIfNeeded();
  
  server.send(200, "text/plain", "Measurement started and display updated.");
}

// *** MODIFIED HTML GENERATION ***
String generateHtmlPage() {
  // Use a simple, modern design with Chart.js for the graph
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CO2 Monitor</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@3.7.1/dist/chart.min.js"></script>
<style>
  body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background-color: #f4f4f9; }
  .container { max-width: 800px; margin: auto; background: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
  h1 { color: #333; text-align: center; }
  h2 { color: #555; margin-top: 30px; margin-bottom: 10px; }
  .status-box { text-align: center; padding: 15px; margin: 15px 0; border-radius: 6px; }
  #co2-status { font-size: 2.5em; font-weight: bold; }
  #co2-status-label { font-size: 1.2em; margin-bottom: 10px; }
  .info-grid { display: flex; justify-content: space-around; margin: 20px 0; }
  .info-item { text-align: center; }
  .info-value { font-size: 1.5em; font-weight: bold; color: #555; }
  .info-label { font-size: 0.9em; color: #777; }
  .alert-control { display: flex; justify-content: space-between; align-items: center; margin: 20px 0; padding: 10px; background: #eee; border-radius: 4px; }
  #measure-button { background-color: #007BFF; color: white; border: none; padding: 10px 15px; border-radius: 4px; cursor: pointer; font-size: 1em; transition: background-color 0.3s; }
  #measure-button:hover { background-color: #0056b3; }
  .control-row { display: flex; justify-content: space-between; align-items: center; margin-top: 20px; margin-bottom: 20px; }
  .switch { position: relative; display: inline-block; width: 60px; height: 34px; }
  .switch input { opacity: 0; width: 0; height: 0; }
  .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; border-radius: 34px; }
  .slider:before { position: absolute; content: ""; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }
  input:checked + .slider { background-color: #f44336; }
  input:checked + .slider:before { transform: translateX(26px); }

  .chart-container {
      height: 300px; 
      position: relative;
      margin-bottom: 30px;
  }
</style>
</head>
<body>
<div class="container">
  <h1>CO2 Monitor Status</h1>

  <div class="status-box">
    <div id="co2-status-label">Current CO2 Level</div>
    <div id="co2-status">... ppm</div>
    <div id="co2-health-label" style="font-weight: bold;">Loading...</div>
  </div>

  <div class="info-grid">
    <div class="info-item">
      <div id="temp-value" class="info-value">...°C</div>
      <div class="info-label">Temperature</div>
    </div>
    <div class="info-item">
      <div id="humidity-value" class="info-value">...%</div>
      <div class="info-label">Humidity</div>
    </div>
  </div>

  <div class="control-row">
    <button id="measure-button" onclick="measureNow()">Force Measurement Now</button>
    <div class="alert-control" style="background: none; padding: 0;">
      <span>Flash LED Alert (> 2000 ppm)</span>
      <label class="switch">
        <input type="checkbox" id="alert-toggle" onchange="toggleAlert()">
        <span class="slider"></span>
      </label>
    </div>
  </div>

  <h2>CO2 Level - Last 24 Hours</h2>
  <div class="chart-container">
    <canvas id="co2Chart"></canvas>
  </div>

  <h2>Temperature - Last 24 Hours</h2>
  <div class="chart-container">
    <canvas id="tempChart"></canvas>
  </div>

  <h2>Humidity - Last 24 Hours</h2>
  <div class="chart-container">
    <canvas id="humidityChart"></canvas>
  </div>
</div>

<script>
  // Chart initialization
  const ctxCO2 = document.getElementById('co2Chart').getContext('2d');
  const ctxTemp = document.getElementById('tempChart').getContext('2d');
  const ctxHumidity = document.getElementById('humidityChart').getContext('2d');
  let co2Chart, tempChart, humidityChart;

  function setStatusColor(co2) {
    let color, label;
    if (co2 < 800) { color = '#4CAF50'; label = 'Good'; }
    else if (co2 < 1200) { color = '#FFC107'; label = 'Moderate'; }
    else if (co2 < 1500) { color = '#FF9800'; label = 'High'; }
    else { color = '#F44336'; label = 'Very High'; }

    document.getElementById('co2-status').style.color = color;
    document.getElementById('co2-health-label').innerHTML = label;
  }

  // Fetch current status
  function fetchStatus() {
    fetch('/api/status')
      .then(response => response.json())
      .then(data => {
        document.getElementById('co2-status').innerHTML = data.co2 + ' ppm';
        document.getElementById('temp-value').innerHTML = data.temp + '°C';
        document.getElementById('humidity-value').innerHTML = data.humidity + '%';
        setStatusColor(data.co2);

        // Update toggle state
        document.getElementById('alert-toggle').checked = data.alert_enabled;
      })
      .catch(error => console.error('Error fetching status:', error));
  }

  // Fetch history and render charts
  function fetchHistory() {
    fetch('/api/history')
      .then(response => response.json())
      .then(data => {
        const labels = data.history.map(item => {
          const date = new Date(item.timestamp * 1000);
          return date.toLocaleTimeString('en-US', {hour: '2-digit', minute:'2-digit'});
        });
        const co2Data = data.history.map(item => item.co2);
        const tempData = data.history.map(item => item.temperature);
        const humidityData = data.history.map(item => item.humidity);

        // Destroy previous chart instances
        if(co2Chart) co2Chart.destroy();
        if(tempChart) tempChart.destroy();
        if(humidityChart) humidityChart.destroy();

        // CO2 Chart
        co2Chart = new Chart(ctxCO2, {
          type: 'line',
          data: {
            labels: labels,
            datasets: [{
              label: 'CO2 (ppm)',
              data: co2Data,
              borderColor: '#007BFF',
              backgroundColor: 'rgba(0, 123, 255, 0.1)',
              tension: 0.2,
              pointRadius: 3
            }]
          },
          options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
              y: {
                beginAtZero: false,
                title: { display: true, text: 'CO2 (ppm)' }
              },
              x: {
                title: { display: true, text: 'Time' }
              }
            },
            plugins: {
              legend: { display: false }
            }
          }
        });

        // Temperature Chart
        tempChart = new Chart(ctxTemp, {
          type: 'line',
          data: {
            labels: labels,
            datasets: [{
              label: 'Temperature (°C)',
              data: tempData,
              borderColor: '#FF6384',
              backgroundColor: 'rgba(255, 99, 132, 0.1)',
              tension: 0.2,
              pointRadius: 3
            }]
          },
          options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
              y: {
                beginAtZero: false,
                title: { display: true, text: 'Temperature (°C)' }
              },
              x: {
                title: { display: true, text: 'Time' }
              }
            },
            plugins: {
              legend: { display: false }
            }
          }
        });

        // Humidity Chart
        humidityChart = new Chart(ctxHumidity, {
          type: 'line',
          data: {
            labels: labels,
            datasets: [{
              label: 'Humidity (%)',
              data: humidityData,
              borderColor: '#36A2EB',
              backgroundColor: 'rgba(54, 162, 235, 0.1)',
              tension: 0.2,
              pointRadius: 3
            }]
          },
          options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
              y: {
                beginAtZero: false,
                title: { display: true, text: 'Humidity (%)' }
              },
              x: {
                title: { display: true, text: 'Time' }
              }
            },
            plugins: {
              legend: { display: false }
            }
          }
        });
      })
      .catch(error => console.error('Error fetching history:', error));
  }

  // Toggle LED Alert
  function toggleAlert() {
    const isChecked = document.getElementById('alert-toggle').checked;
    fetch('/set-alert', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: 'enabled=' + (isChecked ? 'true' : 'false')
    })
    .then(response => {
      if (!response.ok) throw new Error('Network response was not ok');
      console.log('Alert toggle successful');
    })
    .catch(error => {
      console.error('Error toggling alert:', error);
      // Revert the toggle on failure
      document.getElementById('alert-toggle').checked = !isChecked;
    });
  }

  // *** NEW: Force immediate measurement ***
  function measureNow() {
    document.getElementById('measure-button').innerHTML = 'Measuring...';
    document.getElementById('measure-button').disabled = true;

    fetch('/measure-now', {
      method: 'POST'
    })
    .then(response => {
      if (!response.ok) throw new Error('Network response was not ok');
      console.log('Measurement forced.');
      // After measurement, refresh status and history
      fetchStatus();
      fetchHistory();
    })
    .catch(error => {
      console.error('Error forcing measurement:', error);
    })
    .finally(() => {
      // Re-enable button after a short delay
      setTimeout(() => {
        document.getElementById('measure-button').innerHTML = 'Force Measurement Now';
        document.getElementById('measure-button').disabled = false;
      }, 3000); 
    });
  }

  // Initial load and periodic update
  document.addEventListener('DOMContentLoaded', () => {
    fetchStatus();
    fetchHistory();
    setInterval(fetchStatus, 5000); // Update status every 5 seconds
    setInterval(fetchHistory, 600000); // Update graph every 10 minutes (matching sample rate)
  });
</script>
</body>
</html>
)rawliteral";
  return html;
}

// *** MODIFIED HISTORY DATA FORMATTING ***
String generateChartData() {
  String data = "";
  int start_index = 0;
  int count = MAX_SAMPLES;

  if (is_history_full) {
    // If full, start after the next write position (history_index)
    start_index = history_index;
  } else {
    // If not full, start from 0 up to the current write position
    start_index = 0;
    count = history_index;
  }

  for (int i = 0; i < count; i++) {
    // Calculate the index in the circular buffer
    int current_i = (start_index + i) % MAX_SAMPLES;

    if (history_data[current_i].co2 > 0) {
      if (data.length() > 0) {
        data += ",";
      }
      data += "{\"co2\":" + String(history_data[current_i].co2) + "," +
              "\"temperature\":" + String(history_data[current_i].temperature, 2) + "," +
              "\"humidity\":" + String(history_data[current_i].humidity, 2) + "," +
              "\"timestamp\":" + String(history_data[current_i].timestamp) + "}";
    }
  }
  return data;
}


// *** REST OF ORIGINAL E-INK FUNCTIONS ***

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
    display.print("Web & E-ink Mode");

  } while (display.nextPage());
}

void updateDisplay(uint16_t co2, float temperature, float humidity) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Title bar
    display.fillRect(0, 0, display.width(), 30, GxEPD_BLACK);
    display.setFont(&FreeMonoBold12pt7b);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(60, 21);
    display.print("CO2 Monitor");

    // CO2 Level (main focus)
    display.setFont(&FreeMonoBold18pt7b);
    int co2Color = getCO2Color(co2);
    display.setTextColor(co2Color);
    display.setCursor(20, 65);
    display.printf("%d ppm", co2);

    // CO2 status label
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(co2Color);
    display.setCursor(20, 85);
    display.print(getCO2Status(co2));

    // Draw colored indicator bar
    int barWidth = map(min((int)co2, 2000), 400, 2000, 10, display.width() - 20);
    display.fillRect(10, 95, barWidth, 8, co2Color);
    display.drawRect(10, 95, display.width() - 20, 8, GxEPD_BLACK);

    // Temperature and Humidity
    display.setFont(&FreeMonoBold12pt7b);
    display.setTextColor(GxEPD_BLACK);

    // Temperature
    display.setCursor(20, 120);
    display.printf("%.1f", temperature);
    display.print("C");

    // Humidity
    display.setCursor(170, 120);
    display.printf("%.1f", humidity);
    display.print("%");

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
  if (co2 < 800) {
    return GxEPD_BLACK;
  } else if (co2 < 1200) {
    return GxEPD_BLACK;
  } else {
    return GxEPD_RED;
  }
}

// ** CORRECTED FUNCTION DEFINITION **
const char* getCO2Status(uint16_t co2) {
  if (co2 < 800) {
    return "Good";
  } else if (co2 < 1200) {
    return "Moderate";
  } else if (co2 < 1500) {
    return "High";
  } else {
    return "Very High";
  }
}