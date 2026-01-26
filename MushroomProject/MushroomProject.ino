/*
 * Board:   Wemos D1 R32 (ESP32)
 * Sketch:  wemosd1r32_dht22_mhz19.ino
 * Version: 1.1
 * Datum:   26.11.2025
 */

// Bibliotheken einbinden
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LiquidCrystal_I2C.h>
#include "RTClib.h"
#include <DHT.h>
#include <MHZ19.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebServer.h>

// OLED SSD1306 Parameter
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C


// LCD1602 Parameter
#define I2C_ADDR    0x27
#define LCD_COLUMNS 16
#define LCD_LINES   2


// DHT22 Parameter
const int DHT_PIN = 27;
#define DHTTYPE DHT22
DHT dht(DHT_PIN, DHTTYPE);
 
// FreeRTOS Tasks für Cores
TaskHandle_t TaskWLAN_OLED;
TaskHandle_t TaskSensor;

// WLAN
const char* ssid = "OvM-Raspi";
const char* password = "abcD1234";

// Webserver auf Port 80
WebServer server(80);

// Sensor Variabeln
float temp;
float hum;
float co2;

// Welche Werte angezeigt werden
int tab = 0;

// Sensor Variabeln werte Bereiche
float  maxTemp = 24;
float  minTemp = 10;


float  maxHum = 95;
float  minHum = 80;


float  maxCO2 = 1500;
float  minCO2 = 800;

// Min Max Werte anpassen für gelben Button
int minMaxSetter = 0;

// LED Pins (optional)
int ledPins[10] = {0, 2, 18, 25};


// Button PINs
const int PIN_BUTTONGR = 23;
const int PIN_BUTTONR = 26;
const int PIN_BUTTONGE = 19;

// Ist der Button gedrückt worden
bool greenButtonPressed = true;
bool redButtonPressed = true;
bool yellowButtonPressed = true;


// MH-Z19 CO₂ Sensor
MHZ19 mhz19;
HardwareSerial mySerial(2); // UART2 für MH-Z19


// Objekte oled
LiquidCrystal_I2C lcd(I2C_ADDR, LCD_COLUMNS, LCD_LINES);
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RTC_DS1307 rtc;

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    // Warten auf die Verbindung zum seriellen Port
  }
  // Mit dem WLAN verbinden
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    display.print(".");
    display.display();
  }
 
  // Webserver-Routen
  server.on("/", handleRoot);     // HTML Dashboard
  server.on("/data", handleData); // JSON API

  server.begin();
  Serial.println("Webserver gestartet!");

  Serial.println("\nVerbindung hergestellt!");
  Serial.print("IP-Adresse: ");
  Serial.println(WiFi.localIP());

  //Buttons
  pinMode(PIN_BUTTONGR, INPUT_PULLUP);
  pinMode(PIN_BUTTONR, INPUT_PULLUP);
  pinMode(PIN_BUTTONGE, INPUT_PULLUP);

  // Starte CO2 Sensor
  dht.begin();


  // UART für MH-Z19: RX=16, TX=17 (freie Pins am ESP32)
  mySerial.begin(9600, SERIAL_8N1, 16, 17);
  mhz19.begin(mySerial);
  mhz19.autoCalibration(false); // Auto-Kalibrierung deaktivieren


  // OLED initialisieren
  if (!oled.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  oled.display();
  delay(2000);
  oled.clearDisplay();


  // LED Pins als Ausgang
  for (int i = 0; i <= 3; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], HIGH);
  }

  // Task für Webserver + OLED + Knöpfe auf Core 0
  xTaskCreatePinnedToCore(
    TaskWLANOLEDcode,
    "TaskWLANOLED",
    10000,
    NULL,
    1,
    &TaskWLAN_OLED,
    0);

  // Task für Sensoren auf Core 1
  xTaskCreatePinnedToCore(
    TaskSensorcode,
    "TaskSensor",
    10000,
    NULL,
    1,
    &TaskSensor,
    1);
}

// Task Webserver + OLED + Led + Knöpfe
void TaskWLANOLEDcode(void * pvParameters) {
  for(;;) {
    // Knöpfe gedrückt
    readButtons();
    // OLED aktualisieren
    SetDisplay();
    // Leds aktualisieren
    ledDisplay();
    // Webserver bedienen
    server.handleClient();
  }
}

// Task Sensoren ablesen und aktualisieren
void TaskSensorcode(void * pvParameters) {
  for(;;) {
    temp = dht.readTemperature();
    hum  = dht.readHumidity();
    co2  = mhz19.getCO2();

    Serial.print("Temp: "); Serial.println(temp);
    Serial.print("Hum: ");  Serial.println(hum);
    Serial.print("CO2: ");  Serial.println(co2);
   
    vTaskDelay(2000 / portTICK_PERIOD_MS); // alle 2 Sekunden
  }
}

// Nicht benötigt wird durch die Tasks auf 2 Threads aufgeteilt
void loop()
{

}

// HTML Dashboard
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>ESP32 Dashboard</title>";
  html += "<style>";
  html += "body { font-family: Arial; background:#f4f4f4; color:#333; text-align:center; }";
  html += ".card { background:#fff; padding:20px; margin:20px auto; width:300px; box-shadow:0 0 10px rgba(0,0,0,0.1); }";
  html += ".bar { height:20px; text-align:right; color:white; padding-right:5px; }";
  html += "</style>";
  html += "<script>setTimeout(()=>{location.reload();},2000);</script>"; // Auto-Refresh alle 2 Sekunden
  html += "</head><body>";
  html += "<h1>Mushroom-Climate-Dashboard</h1>";

  // Temperatur
  float tempPercent = calcPercentage(temp, minTemp, maxTemp);
  String tempColor = (tempPercent >= 75) ? "#4CAF50" : (tempPercent >= 50 ? "#FFC107" : "#F44336");
  html += "<div class='card'><h2>Temperatur</h2>";
  html += "<p>" + String(temp) + " °C</p>";
  html += "<div class='bar' style='width:" + String(tempPercent) + "%;background:" + tempColor + "'>" + String((int)tempPercent) + "%</div></div>";

  // Luftfeuchtigkeit
  float humPercent = calcPercentage(hum, minHum, maxHum);
  String humColor = (humPercent >= 75) ? "#4CAF50" : (humPercent >= 50 ? "#FFC107" : "#F44336");
  html += "<div class='card'><h2>Luftfeuchtigkeit</h2>";
  html += "<p>" + String(hum) + " %</p>";
  html += "<div class='bar' style='width:" + String(humPercent) + "%;background:" + humColor + "'>" + String((int)humPercent) + "%</div></div>";

  // CO₂
  float co2Percent = calcPercentage(co2, minCO2, maxCO2);
  String co2Color = (co2Percent >= 75) ? "#4CAF50" : (co2Percent >= 50 ? "#FFC107" : "#F44336");
  html += "<div class='card'><h2>CO₂</h2>";
  html += "<p>" + String(co2) + " ppm</p>";
  html += "<div class='bar' style='width:" + String(co2Percent) + "%;background:" + co2Color + "'>" + String((int)co2Percent) + "%</div></div>";

  // Gesamtstatus
  float avgPercent = (tempPercent + humPercent + co2Percent) / 3.0;
  String avgColor = (avgPercent >= 75) ? "#4CAF50" : (avgPercent >= 50 ? "#FFC107" : "#F44336");
  html += "<div class='card'><h2>Gesamtstatus</h2>";
  html += "<p>Durchschnitt aller Werte</p>";
  html += "<div class='bar' style='width:" + String(avgPercent) + "%;background:" + avgColor + "'>" + String((int)avgPercent) + "%</div></div>";

  html += "</body></html>";
  server.send(200, "text/html", html);
}

// Json Data
void handleData() {
  // Einzelwerte berechnen
  float tempPercent = calcPercentage(temp, minTemp, maxTemp);
  float humPercent  = calcPercentage(hum, minHum, maxHum);
  float co2Percent  = calcPercentage(co2, minCO2, maxCO2);

  // Durchschnitt
  float avgPercent = (tempPercent + humPercent + co2Percent) / 3.0;

  // Farbe bestimmen
  String statusColor;
  if (avgPercent >= 75) {
    statusColor = "green";
  } else if (avgPercent >= 50) {
    statusColor = "yellow";
  } else {
    statusColor = "red";
  }

  // JSON erstellen
  StaticJsonDocument<300> doc;
  doc["temperature"] = temp;
  doc["humidity"]    = hum;
  doc["co2"]         = co2;
  doc["statusPercent"] = avgPercent;
  doc["statusColor"]   = statusColor;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// Wertet Knopfdrücke aus
void readButtons()
{
  // Tab case nach ->
   if (greenButtonPressed && digitalRead(PIN_BUTTONGR) == LOW )
  {
    if(tab == 2)
    {
      tab = 0;
      greenButtonPressed = false;
    }
    else
    {
      tab += 1;
      greenButtonPressed = false;
    }
   
  }

  // Tab case nach <-
  if (redButtonPressed && digitalRead(PIN_BUTTONR) == LOW )
  {
    if(tab == -3)
    {
      tab = 0;
      redButtonPressed = false;
    }
    else
    {
      tab -= 1;
      redButtonPressed = false;
    }
  }
  
  if (yellowButtonPressed && digitalRead(PIN_BUTTONGE) == LOW )
  {
    minMaxSetter += 1;
    yellowButtonPressed = false;
  }

  // wenn der Button gedrückt gehalten wird, damit kein doppelter Input gelesen wird, werden Buttons erst nach stopp des drückens wieder verfügbar
  if (digitalRead(PIN_BUTTONGR) == HIGH )
  {
    greenButtonPressed = true;
  }
  if (digitalRead(PIN_BUTTONR) == HIGH )
  {
    redButtonPressed = true;
  }
  if (digitalRead(PIN_BUTTONGE) == HIGH )
  {
    yellowButtonPressed = true;
  }
}

// updated je nach case tab die LED Bar nach Güte der Werte 
void ledDisplay()
{
  switch(tab){
  case 1:
  // case 1
  showLedByPercentage(calcPercentage(temp, minTemp, maxTemp));
  break;
  case 2:
  // case 2
  showLedByPercentage(calcPercentage(hum, minHum, maxHum));
  break;
  case 3:
  // case 3
  showLedByPercentage(calcPercentage(co2, minCO2, maxCO2));
  default:
  showLedByPercentage((calcPercentage(co2, minCO2, maxCO2) + calcPercentage(hum, minHum, maxHum) + calcPercentage(temp, minTemp, maxTemp))/3);
  break;
  }
}

// schaltet je nach Prozent Wert die LEDs der LED Bar an und aus 
void showLedByPercentage(float percent) {
  // Alle LEDs erstmal ausschalten
  for (int i = 0; i < 4; i++) {
    digitalWrite(ledPins[i], LOW);
  }

  // Logik abhängig vom Prozentwert
  if (percent >= 100) {
    // alle 4 LEDs
    for (int i = 0; i < 4; i++) {
      digitalWrite(ledPins[i], HIGH);
    }
  } else if (percent >= 75) {
    // 3 LEDs
    for (int i = 0; i < 3; i++) {
      digitalWrite(ledPins[i], HIGH);
    }
  } else if (percent >= 50) {
    // 2 LEDs
    for (int i = 0; i < 2; i++) {
      digitalWrite(ledPins[i], HIGH);
    }
  } else if (percent > 0) {
    // 1 LED
    digitalWrite(ledPins[0], HIGH);
  }
}

// berechnet die prozentuale Abweichung des gemessenden Values zu den minimalen und maximalen Wert
float calcPercentage(float value, float minVal, float maxVal) {
  if (value >= minVal && value <= maxVal) {
    return 100.0; // innerhalb des Min Max Bereichs
  } else if (value < minVal) {
    return (value / minVal) * 100.0; // unterhalb des Minwertes
  } else { // value > maxVal
    return (maxVal / value) * 100.0; // oberhalb des Maxwertes
  }
}

// Updated das OLED Display nach Tab Case und den aktuellen gemessenden Werten
void SetDisplay()
{
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
 switch(tab){
  case (1):
  // case 1 Temperatur Werte mit Min und Max
  Serial.println("Temp 1");
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.print("Temp: " + String(temp) + " C");
  oled.setCursor(0, 10);
  oled.print("Max Temp: " + String(maxTemp) + " C");
  oled.setCursor(0, 20);
  oled.print("Min Temp: " + String(minTemp) + " C");
  oled.display();
  break;
  case (2):
  // case 2 Luftfeuchtigkeit Werte mit Min und Max
  Serial.println("Hum 2");
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.print("Hum: " + String(hum) + " %");
  oled.setCursor(0, 10);
  oled.print("Max Hum: " + String(maxHum) + " %");
  oled.setCursor(0, 20);
  oled.print("Min Hum: " + String(minHum) + " %");
  oled.display();
  break;
    case (3):
  // case 3 CO2 Werte mit Min und Max
  Serial.println("Co2 3");
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.print("CO2: " + String(co2) + " ppm");
  oled.setCursor(0, 10);
  oled.print("Max CO2: " + String(maxCO2) + " ppm");
  oled.setCursor(0, 20);
  oled.print("Min CO2: " + String(minCO2) + " ppm");
  oled.display();


  case (-1):
  // case -1 Temperatur Werte mit Min und Max
  Serial.println("Temp 1");
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.print("Temp: " + String(temp) + " C");
  oled.setCursor(0, 10);
  oled.print("Max Temp: " + String(maxTemp) + " C");
  oled.setCursor(0, 20);
  oled.print("Min Temp: " + String(minTemp) + " C");
  oled.display();
  break;
  case (-2):
  // case -2 Luftfeuchtigkeit Werte mit Min und Max
  Serial.println("Hum 2");
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.print("Hum: " + String(hum) + " %");
  oled.setCursor(0, 10);
  oled.print("Max Hum: " + String(maxHum) + " %");
  oled.setCursor(0, 20);
  oled.print("Min Hum: " + String(minHum) + " %");
  oled.display();
  break;
    case (-3):
  // case -3 CO2 Werte mit Min und Max
  Serial.println("Co2 3");
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.print("CO2: " + String(co2) + " ppm");
  oled.setCursor(0, 10);
  oled.print("Max CO2: " + String(maxCO2) + " ppm");
  oled.setCursor(0, 20);
  oled.print("Min CO2: " + String(minCO2) + " ppm");
  oled.display();
  default:
  Serial.println("Overview");
  // OLED Anzeige Alle Sensor Werte im Überblick
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.print("Temp: " + String(temp) + " C");
  oled.setCursor(0, 10);
  oled.print("Humidity: " + String(hum) + " %");
  oled.setCursor(0, 20);
  oled.print("CO2: " + String(co2) + " ppm");
  oled.display();
  break;
  }
}

