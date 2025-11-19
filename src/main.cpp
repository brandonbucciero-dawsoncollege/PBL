#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define DISPLAY_DIN 23
#define DISPLAY_CLK 18
#define DISPLAY_CS 5
#define DISPLAY_DC 16
#define DISPLAY_RES 17

Adafruit_SSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &SPI, DISPLAY_DC, DISPLAY_RES, DISPLAY_CS);

void setup() {
  SPI.begin(DISPLAY_CLK, -1, DISPLAY_DIN, DISPLAY_CS);
  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(1);
  display.setCursor(0, 0);
  display.println("Hello!");
  display.display();
}

void loop() {}
