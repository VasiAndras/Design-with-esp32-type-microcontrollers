#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_AHTX0.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Define transistor state
bool transistorState = false;

// Define ultrasonic sensor pins
const int trig_pin = 4;
const int echo_pin = 2;

// Sound speed in air
#define SOUND_SPEED 340
#define TRIG_PULSE_DURATION_US 10

// Define ultrasonic sensor variables
long ultrason_duration;
float distance_cm;

// Define the LCD I2C address
#define I2C_ADDRESS 0x27

// Define the LCD screen size
#define LCD_COLUMNS 16
#define LCD_ROWS 2

// Create a LiquidCrystal_I2C object
LiquidCrystal_I2C lcd(I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);

// Replace with your network credentials
const char *ssid = "wifi";
const char *password = "password";

// GPIO pin where the LED is connected
int ledPin = 19; 

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create an Event Source on /events
AsyncEventSource events("/events");

// Json Variable to Hold Sensor Readings
JSONVar readings;

// Timer variables
unsigned long lastTime = 0;
unsigned long timerDelay = 10000;

// Create a sensor object
Adafruit_AHTX0 aht;

// Initialize AHTX0 sensor
void initATHX0()
{
  if (!aht.begin())
  {
    Serial.println("Could not find AHT? Check wiring");
    while (1)
      delay(10);
  }
  Serial.println("AHT10 or AHT20 found");
}

// Get Sensor Readings and return JSON object
String getSensorReadings()
{
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);

  readings["temperature"] = String(temp.temperature);
  readings["humidity"] = String(humidity.relative_humidity);

  String jsonString = JSON.stringify(readings);
  return jsonString;
}

// Initialize SPIFFS
void initSPIFFS()
{
  if (!SPIFFS.begin())
  {
    Serial.println("An error occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
}

// Initialize WiFi
void initWiFi()
{
  for (int i = 1; i <= 3; i++)
  {
    delay(1000);
    Serial.println(i);
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

void setup()
{
  // Initialize ultrasonic sensor pins
  pinMode(trig_pin, OUTPUT);
  pinMode(echo_pin, INPUT);

  // Initialize the LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Live greenhouse");
  lcd.setCursor(0, 1);
  lcd.print("monitoring syst.");

  // Wait 2 seconds
  delay(2000);

  // Initialize the LED pin as an output
  pinMode(ledPin, OUTPUT);

  // Initialize serial communication
  Serial.begin(115200);
  
  //Initialize AHTX0 sensor
  initATHX0();

  // Initialize WiFi and SPIFFS
  initWiFi();
  initSPIFFS();

  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    // Handle Web Server Root URL
    request->send(SPIFFS, "/index.html", "text/html");
  });

  server.serveStatic("/", SPIFFS, "/");

  // Handle Web Server Events URL
  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    transistorState = !transistorState;
    request->send(200, "text/plain", "LED toggled");
    Serial.println("toggle");
    delay(500);
  });

  // Handle Web Server Events URL
  server.on("/temperature", HTTP_POST, [](AsyncWebServerRequest *request)
  {
    JSONVar response;
    response["status"] = "success";
    response["message"] = "Temperature received";
    request->send(200, "application/json", JSON.stringify(response));
  });

  // Handle Web Server Events URL
  server.on("/readings", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    String json = getSensorReadings();
    request->send(200, "application/json", json);
    json = String();
  });

  // onConnect callback function
  events.onConnect([](AsyncEventSourceClient *client)
  {
    if (client->lastId())
    {
      Serial.printf("Client reconnected! Last message ID: %u\n", client->lastId());
    }
    client->send("hello!", NULL, millis(), 10000);
  });
  server.addHandler(&events);

  server.begin();
}

// Main loop
void loop()
{
  // Read sensor data every x seconds
  if ((millis() - lastTime) > timerDelay)
  {
    events.send("ping", NULL, millis());
    events.send(getSensorReadings().c_str(), "new_readings", millis());
    lastTime = millis();
  }

  // Write sensor data to serial monitor
  Serial.println(getSensorReadings());

  // Write sensor data to LCD
  JSONVar data = JSON.parse(getSensorReadings());
  String temperature = data["temperature"];
  String humidity = data["humidity"];

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Temp.: " + temperature + " C");
  lcd.setCursor(0, 1);
  lcd.print("Humidity: " + humidity + "%");

  // Wait 3 seconds
  delay(3000);

  // Write state of ventillator to LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("State of ");
  lcd.setCursor(0, 1);
  
  if (digitalRead(ledPin) == 1)
  {
    lcd.print("ventilator: ON");
  }
  else
  {
    lcd.print("ventilator: OFF");
  }

  // Wait 3 seconds
  delay(3000);

  // Write distance to LCD
  digitalWrite(trig_pin, LOW);
  delayMicroseconds(2);
  digitalWrite(trig_pin, HIGH);
  delayMicroseconds(TRIG_PULSE_DURATION_US);
  digitalWrite(trig_pin, LOW);
  delay(100);

  ultrason_duration = pulseIn(echo_pin, HIGH);
  delay(100);
  
  // Calculate distance in cm
  distance_cm = ultrason_duration * SOUND_SPEED / 2 * 0.0001;
  Serial.print("Distance (cm): ");
  Serial.println(distance_cm);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Distance: ");
  lcd.setCursor(0, 1);
  lcd.print(String(distance_cm / 10) + " cm");

  // Wait 3 seconds
  delay(3000);

  double temp = atof(temperature.c_str());
  double hum = atof(humidity.c_str());

  // Turn on LED if temperature is above 29 degrees or humidity is above 60%
  // Or if the ventillator is turned on manually from the web interface
  if (temp > 29 || hum > 60 || transistorState == true)
  {
    digitalWrite(ledPin, HIGH);
  }
  else
  {
    digitalWrite(ledPin, LOW);
    transistorState = false;
  }
}
