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
#include <HardwareSerial.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
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

const char* topic = "api/data";
 
// FreeRTOS Tasks
TaskHandle_t TaskWLAN_OLED;
TaskHandle_t TaskSensor;

// WLAN/MQTT
const char* ssid = "OvM-Raspi";
const char* password = "abcD1234";
const char* url = "https://www.heise.de/";

const char* mqtt_server = "fa7604796f27494dbdfb0104438c6df0.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "MushroomHelper";
const char* mqtt_pass = "mushrooM1";
const char* topic = "api/data";

// Webserver auf Port 80
WebServer server(80);

WiFiClientSecure espClient;
PubSubClient client(espClient);


float temp;
float hum;
float co2;


int tab = 0;


float  maxTemp = 24;
float  minTemp = 10;


float  maxHum = 95;
float  minHum = 80;


float  maxCO2 = 1500;
float  minCO2 = 800;


int minMaxSetter = 0;
// LED Pins (optional)
int ledPins[10] = {0, 2, 18, 25};


// Button PINs
const int PIN_BUTTONGR = 23;
const int PIN_BUTTONR = 26;
const int PIN_BUTTONGE = 19;


bool bGreen = true;
bool bRed = true;
bool bYellow = true;


// MH-Z19 CO₂ Sensor
MHZ19 mhz19;
HardwareSerial mySerial(2); // UART2 für MH-Z19


// Objekte
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
 
  // 5. Server starten
  server.on("/", handleRoot);    // Das Dashboard
  server.on("/data", handleData); // Die API
  server.begin();

  Serial.println("\nVerbindung hergestellt!");
  Serial.print("IP-Adresse: ");
  Serial.println(WiFi.localIP());

  //Buttons
  pinMode(PIN_BUTTONGR, INPUT_PULLUP);
  pinMode(PIN_BUTTONR, INPUT_PULLUP);
  pinMode(PIN_BUTTONGE, INPUT_PULLUP);


  //Serial.begin(9600); // USB Serial Monitor
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
  for (int i = 0; i <= 9; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], HIGH);
  }


  //broker
  espClient.setInsecure();
 
  client.setServer(mqtt_server, mqtt_port);

  // Task für WLAN + OLED auf Core 0
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

// --- Task WLAN + OLED ---
void TaskWLANOLEDcode(void * pvParameters) {
  for(;;) {
    // WLAN prüfen
    wLan();

    // MQTT loop
    if (!client.connected()) {
      mqttconnect();
    }
    client.loop();
    // Buttons pressed
    readButtons();
    // OLED aktualisieren
    SetDisplay();
    // Leds aktualisieren
    ledDisplay();
    vTaskDelay(100 / portTICK_PERIOD_MS); // alle 0.1 Sekunden
  }
}

// --- Task Sensoren ---
void TaskSensorcode(void * pvParameters) {
  for(;;) {
    temp = dht.readTemperature();
    hum  = dht.readHumidity();
    co2  = mhz19.getCO2();

    Serial.print("Temp: "); Serial.println(temp);
    Serial.print("Hum: ");  Serial.println(hum);
    Serial.print("CO2: ");  Serial.println(co2);

    // JSON für MQTT erstellen
    StaticJsonDocument<200> doc;
    doc["temperature"] = temp;
    doc["humidity"]    = hum;
    doc["co2"]         = co2;
    doc["Enviroment Status"] = showLedByPercentage((calcPercentage(co2, minCO2, maxCO2) + calcPercentage(hum, minHum, maxHum) + calcPercentage(temp, minTemp, maxTemp))/3);

    char buffer[256];
    serializeJson(doc, buffer);

    // Publish
    if (client.connected()) {
      client.publish(topic, buffer);
      Serial.println("MQTT Daten gesendet: ");
      Serial.println(buffer);
    }

    vTaskDelay(2000 / portTICK_PERIOD_MS); // alle 2 Sekunden
  }
}

void loop()
{

}

void wLan() {
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode > 0) {
    Serial.printf("HTTP-GET an %s, Code: %d\n", url, httpCode);
  } else {
    Serial.printf("HTTP-GET fehlgeschlagen: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}


void readButtons()
{
   if (bGreen && digitalRead(PIN_BUTTONGR) == LOW )
  {
    if(tab == 2)
    {
      tab = 0;
      bGreen = false;
    }
    else
    {
      tab += 1;
      bGreen = false;
    }
   
  }
  if (bRed && digitalRead(PIN_BUTTONR) == LOW )
  {
    if(tab == -3)
    {
      tab = 0;
      bRed = false;
    }
    else
    {
      tab -= 1;
      bRed = false;
    }
  }
  if (bYellow && digitalRead(PIN_BUTTONGE) == LOW )
  {
    minMaxSetter += 1;
    bYellow = false;
  }


  if (digitalRead(PIN_BUTTONGR) == HIGH )
  {
    bGreen = true;
  }
  if (digitalRead(PIN_BUTTONR) == HIGH )
  {
    bRed = true;
  }
  if (digitalRead(PIN_BUTTONGE) == HIGH )
  {
    bYellow = true;
  }
}


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

float calcPercentage(float value, float minVal, float maxVal) {
  if (value >= minVal && value <= maxVal) {
    return 100.0; // innerhalb des Bereichs
  } else if (value < minVal) {
    return (value / minVal) * 100.0; // unterhalb
  } else { // value > maxVal
    return (maxVal / value) * 100.0; // oberhalb
  }
}
void SetDisplay()
{
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
 switch(tab){
  case (1):
  // case 1
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
  // case 2
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
  // case 3
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
  // case 1
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
  // case 2
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
  // case 3
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
  // OLED Anzeige
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
  void mqttconnect() {
  while (!client.connected()) {
    Serial.print("Verbinde mit MQTT...");
    if (client.connect("WemosD1R32", mqtt_user, mqtt_pass)) {
      Serial.println("MQTT verbunden");
    } else {
      Serial.print("Fehler, rc=");
      Serial.print(client.state());
      delay(2000);
    }
  }
}

