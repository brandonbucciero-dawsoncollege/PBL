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

// Initialization
Adafruit_BME280 bme;
Adafruit_SSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &SPI, DISPLAY_DC, DISPLAY_RES, DISPLAY_CS);

void setup() {
  // Start I2C BME280
  Wire.begin();
  bme.begin(0x76);
  display.begin(SSD1306_SWITCHCAPVCC); // Start display
  // Show splash screen for 2 seconds before main loop
  display.display();
  delay(2000);
  display.clearDisplay();
}

void loop() {
  // Setup display content
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(1);
  // Read and display BME280 data
  display.print("Temp: ");
  display.print(bme.readTemperature());
  display.println(" C");
  display.print("Pres: ");
  display.print(bme.readPressure() / 1000.0F);
  display.println(" kPa");
  display.print("Humi: ");
  display.print(bme.readHumidity());
  display.println(" %");
  display.display();
  delay(1000); // Update every second
}
