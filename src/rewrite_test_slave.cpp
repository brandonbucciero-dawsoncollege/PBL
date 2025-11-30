#include <Arduino.h>           // Core Arduino functions (setup, loop, pinMode, digitalWrite, etc.)
#include <Wire.h>              // I2C communication library (used by BME280, etc.)
#include <SPI.h>               // SPI communication library (used by OLED in software SPI mode)
#include <Adafruit_GFX.h>      // Graphics library base class for displays (shapes, text, etc.)
#include <Adafruit_SSD1306.h>  // Driver for SSD1306 OLED displays
#include <Adafruit_Sensor.h>   // Common sensor interface (not used directly but required by BME280 lib)
#include <Adafruit_BME280.h>   // BME280 temperature/humidity/pressure sensor library
#include <WiFi.h>              // ESP32 WiFi support
#include <WebServer.h>         // Lightweight HTTP web server for ESP32

// ================================================================
//  USER CONFIGURATION (EDIT THIS FOR EACH SLAVE DEVICE)
// ================================================================
const char* ssid     = "YOUR_HOME_WIFI_NAME";      // <--- YOUR HOME WIFI NAME
const char* password = "YOUR_HOME_WIFI_PASSWORD";  // <--- YOUR HOME WIFI PASSWORD

// STATIC IP SETUP (Must be unique for every device!)
// Example: Master=.50, Bedroom=.51, Kitchen=.52
IPAddress local_IP(192, 168, 1, 51);      // <--- CHANGE THIS (Unique IP)
IPAddress gateway(192, 168, 1, 1);        // <--- YOUR ROUTER IP
IPAddress subnet(255, 255, 255, 0);       // <--- YOUR SUBNET MASK

// DEVICE IDENTIFICATION (MULTI-ROOM SUPPORT)
// Change this string on each device: e.g. "Pat - Bedroom", "Pat - Living Room", etc.
const char* deviceLocation = "Pat - Bedroom";
// ================================================================

// ---------- RGB LED pins (ALERT RGB) ----------
#define RED_PIN   25           // GPIO 25 controls the red channel of main RGB LED (alert LED)
#define GREEN_PIN 26           // GPIO 26 controls the green channel of main RGB LED (alert LED)
#define BLUE_PIN  27           // GPIO 27 controls the blue channel of main RGB LED (alert LED)

// ---------- SMD RGB pins (GRAPH RGB) ----------
#define SMD_R_PIN 14           // GPIO 14 controls the red channel of SMD RGB (graph mode indicator)
#define SMD_G_PIN 12           // GPIO 12 controls the green channel of SMD RGB (graph mode indicator)
#define SMD_B_PIN 13           // GPIO 13 controls the blue channel of SMD RGB (graph mode indicator)

// ---------- Button pin ----------
#define BUTTON_PIN 32          // GPIO 32 is used for the push button input

// ---------- BME + OLED ----------
Adafruit_BME280 bme;           // Create a BME280 object to talk to the sensor
#define BME_ADDR 0x76          // I2C address of the BME280 module (commonly 0x76 or 0x77)

// Screen size for OLED
#define SCREEN_WIDTH  128      // OLED display width in pixels
#define SCREEN_HEIGHT 64       // OLED display height in pixels

// OLED pins for software SPI
#define OLED_MOSI   23         // Data line for OLED (MOSI) in software SPI mode
#define OLED_CLK    18         // Clock line for OLED (SCK) in software SPI mode
#define OLED_DC     16         // Data/Command pin for OLED: high=data, low=command
#define OLED_CS     5          // Chip Select pin for OLED
#define OLED_RESET  17         // Reset pin for OLED (resets the display controller)

// Initialize OLED display object with software SPI pins
Adafruit_SSD1306 display(
  SCREEN_WIDTH,                // Width of display in pixels
  SCREEN_HEIGHT,               // Height of display in pixels
  OLED_MOSI,                   // MOSI pin for software SPI
  OLED_CLK,                    // CLK pin for software SPI
  OLED_DC,                     // D/C pin
  OLED_RESET,                  // RESET pin
  OLED_CS                      // CS pin
);

// Thresholds for alerts
const float TEMP_MIN = 20.0;   // Minimum comfortable/normal temperature in °C
const float TEMP_MAX = 25.0;   // Maximum comfortable/normal temperature in °C
const float HUM_MIN  = 30.0;   // Minimum comfortable/normal humidity in %
const float HUM_MAX  = 40.0;   // Maximum comfortable/normal humidity in %

// ---------- WiFi / Web ----------
WebServer server(80);          // Create a web server object that listens on port 80 (HTTP)

// ---------- Data logging buffer ----------
const int MAX_READINGS = 100;  // Maximum number of logged samples in the circular buffer
int writeIndex = 0;            // Index where next reading will be stored in the buffer
int sampleCount = 0;           // Number of valid samples currently stored (up to MAX_READINGS)

// Structure representing one logged sensor reading
struct LogEntry {
  unsigned long time;          // Timestamp (milliseconds since boot) when sample was taken
  float temp;                  // Temperature value in °C
  float pres;                  // Pressure value in kPa
  float humi;                  // Humidity value in %
} history[MAX_READINGS];       // Array of logged readings (circular buffer)

// ---------- Screen mode + button ----------
int screenMode = 0;                     // Current screen/graph mode: 0=Temp, 1=Humidity, 2=Pressure
unsigned long lastButtonTime = 0;       // Placeholder for debounce timing (not used now)
const unsigned long DEBOUNCE_MS = 200;  // Debounce time in ms (kept for future use)

// ---------- OLED sleep control ----------
bool oledOn = true;                     // Tracks if OLED is currently on or off
const unsigned long LONG_PRESS_MS = 1500;  // Duration (ms) to consider a press as "long press"
bool buttonPrev = HIGH;                 // Previous state of the button (HIGH = not pressed with INPUT_PULLUP)
unsigned long buttonPressStart = 0;     // Time when the button was first pressed

// --------------------------------------------------------
//  LOW-LEVEL HELPERS
// --------------------------------------------------------

void datalog(float t, float p, float h) {
  history[writeIndex] = { millis(), t, p, h };
  writeIndex = (writeIndex + 1) % MAX_READINGS;
  if (sampleCount < MAX_READINGS) sampleCount++;
}

// RGB helper for main ALERT RGB LED
void setRGB(bool r, bool g, bool b) {
  digitalWrite(RED_PIN,   r ? HIGH : LOW);
  digitalWrite(GREEN_PIN, g ? HIGH : LOW);
  digitalWrite(BLUE_PIN,  b ? HIGH : LOW);
}

// SMD RGB helper (graph mode indicator LED)
void setSMD(bool r, bool g, bool b) {
  digitalWrite(SMD_R_PIN, r ? HIGH : LOW);
  digitalWrite(SMD_G_PIN, g ? HIGH : LOW);
  digitalWrite(SMD_B_PIN, b ? HIGH : LOW);
}

// Map a sensor value to a vertical pixel (Y coordinate) on the OLED graph area
int mapToY(float val, float minVal, float maxVal) {
  if (val < minVal) val = minVal;
  if (val > maxVal) val = maxVal;

  const int topMargin = 16;
  const int bottom = SCREEN_HEIGHT - 1;
  const int graphH = bottom - topMargin;

  float norm = (val - minVal) / (maxVal - minVal);
  int y = bottom - (int)(norm * (graphH - 1));
  return y;
}

// --------------------------------------------------------
//  ALERT + STATUS LOGIC
// --------------------------------------------------------

void handleAlerts(float t, float h) {
  bool warn = (t < TEMP_MIN || t > TEMP_MAX || h < HUM_MIN || h > HUM_MAX);
  bool crit = (t > TEMP_MAX + 3 || h > HUM_MAX + 10);

  if (crit)      setRGB(true, false, false);   // RED
  else if (warn) setRGB(true, true,  false);   // YELLOW
  else           setRGB(false, true,  false);  // GREEN
}

void updateSmdForScreen(int mode) {
  if (mode == 0) {                 // Temperature mode
    setSMD(true, false, false);    // RED
  } else if (mode == 1) {          // Humidity mode
    setSMD(false, false, true);    // BLUE
  } else {                         // Pressure mode
    setSMD(false, true, false);    // GREEN
  }
}

// --------------------------------------------------------
//  OLED SLEEP / WAKE
// --------------------------------------------------------

void oledWake() {
  display.ssd1306_command(SSD1306_DISPLAYON);
  oledOn = true;
}

void oledSleep() {
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  oledOn = false;
}

// --------------------------------------------------------
//  WEB HANDLERS
// --------------------------------------------------------

void handleData() {
  float t   = bme.readTemperature();
  float h   = bme.readHumidity();
  float kpa = bme.readPressure() / 1000.0F;

  String json = "{";
  json += "\"tempC\":" + String(t, 2) + ",";
  json += "\"hum\":"   + String(h, 2) + ",";
  json += "\"kpa\":"   + String(kpa, 2) + ",";
  json += "\"location\":\"" + String(deviceLocation) + "\"";
  json += "}";

  // !!! IMPORTANT CHANGE: Allow Cross-Origin Requests !!!
  // This allows the Master Dashboard to read data from this Slave
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleRoot() {
  // Simple status page for slave devices
  String html = "<html><body><h1>" + String(deviceLocation) + "</h1>";
  html += "<p>Mode: SLAVE NODE</p>";
  html += "<p>IP Address: " + WiFi.localIP().toString() + "</p>";
  html += "<p>View the full dashboard on the Master device.</p></body></html>";
  server.send(200, "text/html", html);
}

// --------------------------------------------------------
//  BUTTON HANDLER
// --------------------------------------------------------

void handleButton() {
  int raw = digitalRead(BUTTON_PIN);   // INPUT_PULLUP: LOW = pressed

  if (raw == LOW && buttonPrev == HIGH) {
    buttonPressStart = millis();
  }

  if (raw == HIGH && buttonPrev == LOW) {
    unsigned long pressTime = millis() - buttonPressStart;

    if (pressTime >= LONG_PRESS_MS) {
      if (oledOn) {
        oledSleep();
      } else {
        oledWake();
      }
    } else {
      screenMode = (screenMode + 1) % 3;
    }
  }

  buttonPrev = raw;
}

// --------------------------------------------------------
//  GRAPH DRAWING FUNCTIONS (OLED ONLY)
// --------------------------------------------------------

void drawTempGraph(float currentT) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print("Temp (C)  Now: ");
  display.print(currentT, 1);

  if (sampleCount < 2) {
    display.setCursor(0, 16);
    display.print("Collecting data...");
    display.display();
    return;
  }

  int points = sampleCount;
  if (points > SCREEN_WIDTH) points = SCREEN_WIDTH;

  int lastIdx = (writeIndex - 1 + MAX_READINGS) % MAX_READINGS;

  const int topMargin = 16;
  const int bottom = SCREEN_HEIGHT - 1;

  for (int x = 0; x < points - 1; x++) {
    int idx0 = (lastIdx - (points - 1 - x) + MAX_READINGS) % MAX_READINGS;
    int idx1 = (lastIdx - (points - 2 - x) + MAX_READINGS) % MAX_READINGS;

    float v0 = history[idx0].temp;
    float v1 = history[idx1].temp;

    int y0 = mapToY(v0, 0.0, 40.0);
    int y1 = mapToY(v1, 0.0, 40.0);

    display.drawLine(x, y0, x + 1, y1, SSD1306_WHITE);
  }

  display.drawLine(0, topMargin, 0, bottom, SSD1306_WHITE);
  display.drawLine(0, bottom, SCREEN_WIDTH - 1, bottom, SSD1306_WHITE);

  display.setCursor(2, topMargin + 2);
  display.print("40");
  display.setCursor(2, bottom - 8);
  display.print("0");

  display.setCursor(SCREEN_WIDTH - 20, bottom - 8);
  display.print("t");

  display.display();
}

void drawHumGraph(float currentH) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print("Hum (%)   Now: ");
  display.print(currentH, 1);

  if (sampleCount < 2) {
    display.setCursor(0, 16);
    display.print("Collecting data...");
    display.display();
    return;
  }

  int points = sampleCount;
  if (points > SCREEN_WIDTH) points = SCREEN_WIDTH;

  int lastIdx = (writeIndex - 1 + MAX_READINGS) % MAX_READINGS;

  const int topMargin = 16;
  const int bottom = SCREEN_HEIGHT - 1;

  for (int x = 0; x < points - 1; x++) {
    int idx0 = (lastIdx - (points - 1 - x) + MAX_READINGS) % MAX_READINGS;
    int idx1 = (lastIdx - (points - 2 - x) + MAX_READINGS) % MAX_READINGS;

    float v0 = history[idx0].humi;
    float v1 = history[idx1].humi;

    int y0 = mapToY(v0, 0.0, 100.0);
    int y1 = mapToY(v1, 0.0, 100.0);

    display.drawLine(x, y0, x + 1, y1, SSD1306_WHITE);
  }

  display.drawLine(0, topMargin, 0, bottom, SSD1306_WHITE);
  display.drawLine(0, bottom, SCREEN_WIDTH - 1, bottom, SSD1306_WHITE);

  display.setCursor(2, topMargin + 2);
  display.print("100");
  display.setCursor(2, bottom - 8);
  display.print("0");

  display.setCursor(SCREEN_WIDTH - 20, bottom - 8);
  display.print("t");

  display.display();
}

void drawPresGraph(float currentP) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print("Pres (kPa) Now: ");
  display.print(currentP, 1);

  if (sampleCount < 2) {
    display.setCursor(0, 16);
    display.print("Collecting data...");
    display.display();
    return;
  }

  int points = sampleCount;
  if (points > SCREEN_WIDTH) points = SCREEN_WIDTH;

  int lastIdx = (writeIndex - 1 + MAX_READINGS) % MAX_READINGS;

  const int topMargin = 16;
  const int bottom = SCREEN_HEIGHT - 1;

  for (int x = 0; x < points - 1; x++) {
    int idx0 = (lastIdx - (points - 1 - x) + MAX_READINGS) % MAX_READINGS;
    int idx1 = (lastIdx - (points - 2 - x) + MAX_READINGS) % MAX_READINGS;

    float v0 = history[idx0].pres;
    float v1 = history[idx1].pres;

    int y0 = mapToY(v0, 950.0, 1050.0);
    int y1 = mapToY(v1, 950.0, 1050.0);

    display.drawLine(x, y0, x + 1, y1, SSD1306_WHITE);
  }

  display.drawLine(0, topMargin, 0, bottom, SSD1306_WHITE);
  display.drawLine(0, bottom, SCREEN_WIDTH - 1, bottom, SSD1306_WHITE);

  display.setCursor(2, topMargin + 2);
  display.print("1050");
  display.setCursor(2, bottom - 8);
  display.print("950");

  display.setCursor(SCREEN_WIDTH - 20, bottom - 8);
  display.print("t");

  display.display();
}

// --------------------------------------------------------
//  SENSOR + SERIAL HANDLING
// --------------------------------------------------------

// Read temperature, humidity, and pressure from BME280
void readSensors(float &t, float &h, float &p) {
  t = bme.readTemperature();
  h = bme.readHumidity();
  p = bme.readPressure() / 1000.0F;   // kPa
}

/*
  Human-readable Serial print on every sample, plus IP shown periodically.
*/
void printSerialLine(unsigned long now, float t, float p, float h) {
  static unsigned long sampleNum = 0;
  static unsigned long lastIpPrint = 0;   // for periodic IP display (in ms)

  sampleNum++;

  Serial.print("#");
  Serial.print(sampleNum);
  Serial.print("  t=");
  Serial.print(now);
  Serial.print(" ms  T=");
  Serial.print(t, 1);
  Serial.print(" C  H=");
  Serial.print(h, 1);
  Serial.print(" %  P=");
  Serial.print(p, 1);
  Serial.print(" kPa");

  // Print IP about every 5 seconds so you can always see it in Serial Monitor
  if (now - lastIpPrint >= 5000) {
    Serial.print("  | IP: ");
    Serial.print(WiFi.localIP());            // Shows local Static IP
    lastIpPrint = now;
  }

  Serial.println();
}

void updateGraphs(float t, float h, float p) {
  if (!oledOn) return;

  if (screenMode == 0) {
    drawTempGraph(t);
  } else if (screenMode == 1) {
    drawHumGraph(h);
  } else {
    drawPresGraph(p);
  }
}

void doPeriodicSampling() {
  static unsigned long last = 0;
  unsigned long now = millis();

  if (now - last < 1000) return;   // 1 Hz
  last = now;

  float t, h, p;
  readSensors(t, h, p);
  datalog(t, p, h);
  handleAlerts(t, h);
  updateSmdForScreen(screenMode);
  printSerialLine(now, t, p, h);   // <-- new human-readable serial output + IP
  updateGraphs(t, h, p);
}

// --------------------------------------------------------
//  INITIALIZATION HELPERS
// --------------------------------------------------------

void initPins() {
  pinMode(RED_PIN,   OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN,  OUTPUT);

  pinMode(SMD_R_PIN, OUTPUT);
  pinMode(SMD_G_PIN, OUTPUT);
  pinMode(SMD_B_PIN, OUTPUT);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  setRGB(false, false, false);
  setSMD(false, false, false);
}

// !!! CHANGED: INITIALIZE WIFI STATION MODE (Connect to Router) !!!
void initWiFiSTA() {
  WiFi.mode(WIFI_STA);

  // Configure Static IP
  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("STA Failed to configure");
  }

  WiFi.begin(ssid, password);
  
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());
}

void initWebServer() {
  server.on("/",    handleRoot);
  server.on("/data", handleData);
  server.begin();
}

void initSensorsAndDisplay() {
  Wire.begin();
  bme.begin(BME_ADDR);

  display.begin(SSD1306_SWITCHCAPVCC);
  display.clearDisplay();
  oledOn = true;
}

// --------------------------------------------------------
//  SETUP & LOOP
// --------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(500);

  initPins();
  initWiFiSTA();    // <--- CHANGED FROM AP TO STA
  initWebServer();
  initSensorsAndDisplay();

  Serial.println();
  Serial.println("=== ESP32 BME280 Monitor (SLAVE) ===");
  Serial.print("Device location: ");
  Serial.println(deviceLocation);
  Serial.print("Connected to SSID: ");
  Serial.println(ssid);
  Serial.println("Open Serial Monitor (115200) to see live values and IP.");
}

void loop() {
  server.handleClient();
  handleButton();
  doPeriodicSampling();
}
