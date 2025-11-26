// Libraries
#include <Arduino.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <Wire.h>

// BME280 configuration
#define BME_SDA 21
#define BME_SCL 22

// Display configuration
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define DISPLAY_DIN 23
#define DISPLAY_CLK 18
#define DISPLAY_CS 5
#define DISPLAY_DC 16
#define DISPLAY_RES 17

// LED configuration
#define LED_R 25
#define LED_G 26
#define LED_B 27

// Warning thresholds
int MIN_TEMPERATURE = 15;
int MAX_TEMPERATURE = 30;
int MIN_HUMIDITY = 30;
int MAX_HUMIDITY = 60;

// Data logging
const int MAX_READINGS = 100;
int writeIndex = 0;
struct Datalog {
  ulong time;
  float temp;
  float pres;
  float humi;
}
history[MAX_READINGS];

// Initialization
Adafruit_BME280 bme;
Adafruit_SSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &SPI, DISPLAY_DC, DISPLAY_RES, DISPLAY_CS);

// RGB control function
void rgb(int r, int g, int b) {
  analogWrite(LED_R, r);
  analogWrite(LED_G, g);
  analogWrite(LED_B, b);
}

// Data logging function
void datalog(float temp, float pres, float humi) {
  history[writeIndex].time = millis();
  history[writeIndex].temp = temp;
  history[writeIndex].pres = pres;
  history[writeIndex].humi = humi;
  writeIndex++;
  if (writeIndex >= MAX_READINGS) {
    writeIndex = 0;
  }
}

void setup() {
  Serial.begin(9600);
  // RGB initialization
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  rgb(0, 0, 0);
  // Start I2C BME280
  Wire.begin();
  bme.begin(BME280_ADDRESS_ALTERNATE);
  display.begin(SSD1306_SWITCHCAPVCC); // Start display
  // Show splash screen for 2 seconds before main loop
  display.display();
  delay(2000);
  display.clearDisplay();
}

void loop() {
  // Get values
  float temperature = bme.readTemperature();      // °C
  float pressure = bme.readPressure() / 1000.0F;  // kPa
  float humidity = bme.readHumidity();            // %
  datalog(temperature, pressure, humidity);
  rgb(0, 255, 0); // RGB green if conditions are normal
  // Setup display content
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  // Read and display BME280 data
  display.print("Temperature:");
  display.print(temperature);
  display.println(" C");
  display.print("Pressure   :");
  display.print(pressure);
  display.println(" kPa");
  display.print("Humidity   :");
  display.print(humidity);
  display.println(" %");
  // Warnings
  if (temperature > MAX_TEMPERATURE) {
    display.print("HOT! ");
    rgb(255, 0, 0);
  } else if (temperature < MIN_TEMPERATURE) {
    display.print("COLD! ");
    rgb(255, 0, 0);
  }
  if (humidity > MAX_HUMIDITY) {
    display.print("HUMID! ");
    rgb(255, 0, 0);
  } else if (humidity < MIN_HUMIDITY) {
    display.print("DRY! ");
    rgb(255, 0, 0);
  }
  // TEMPORARY only for testing struct for data logging
  Serial.print("Time: ");
  Serial.println(history[writeIndex - 1].time);
  Serial.print("Temperature: ");
  Serial.println(history[writeIndex - 1].temp);
  Serial.print("Pressure: ");
  Serial.println(history[writeIndex - 1].pres);
  Serial.print("Humidity: ");
  Serial.println(history[writeIndex - 1].humi);
  // Update display every 2 seconds
  display.display();
  delay(2000);
}
