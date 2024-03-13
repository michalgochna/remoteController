/**
 * ----------------------------------------------------------------------------
 * ESP32 Remote Control with WebSocket
 * ----------------------------------------------------------------------------
 * © 2020 Stéphane Calderoni
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "AsyncJson.h"
#include <array>


// ----------------------------------------------------------------------------
// Definition of macros
// ----------------------------------------------------------------------------

#define LED_PIN   26
#define BTN_PIN   22
#define HTTP_PORT 80
#define DEVICE_TYPE "1d"
#define NUMBER_OF_AXES 1
#define AXIS_LIMIT 80


// ----------------------------------------------------------------------------
// Definition of global constants
// ----------------------------------------------------------------------------

// Button debouncing
const uint8_t DEBOUNCE_DELAY = 10; // in milliseconds

// WiFi credentials
const char *WIFI_SSID = "Orange_Swiatlowod_D850";
const char *WIFI_PASS = "Gamblersdice";

int currentPositionSteps = 0;
float currentPosition = 0.0;
float stepsToMilimeters = 0.01;
bool isHomed = false;

// ----------------------------------------------------------------------------
// Definition of the LED component
// ----------------------------------------------------------------------------

struct Led {
    // state variables
    uint8_t pin;
    bool    on;

    // methods
    void update() {
        digitalWrite(pin, on ? HIGH : LOW);
    }
};

// ----------------------------------------------------------------------------
// Definition of the Button component
// ----------------------------------------------------------------------------

struct Button {
    // state variables
    uint8_t  pin;
    bool     lastReading;
    uint32_t lastDebounceTime;
    uint16_t state;

    // methods determining the logical state of the button
    bool pressed()                { return state == 1; }
    bool released()               { return state == 0xffff; }
    bool held(uint16_t count = 0) { return state > 1 + count && state < 0xffff; }

    // method for reading the physical state of the button
    void read() {
        // reads the voltage on the pin connected to the button
        bool reading = digitalRead(pin);

        // if the logic level has changed since the last reading,
        // we reset the timer which counts down the necessary time
        // beyond which we can consider that the bouncing effect
        // has passed.
        if (reading != lastReading) {
            lastDebounceTime = millis();
        }

        // from the moment we're out of the bouncing phase
        // the actual status of the button can be determined
        if (millis() - lastDebounceTime > DEBOUNCE_DELAY) {
            // don't forget that the read pin is pulled-up
            bool pressed = reading == LOW;
            if (pressed) {
                     if (state  < 0xfffe) state++;
                else if (state == 0xfffe) state = 2;
            } else if (state) {
                state = state == 0xffff ? 0 : 0xffff;
            }
        }

        // finally, each new reading is saved
        lastReading = reading;
    }
};

// ----------------------------------------------------------------------------
// Definition of global variables
// ----------------------------------------------------------------------------

Led    onboard_led = { LED_BUILTIN, false };
Led    led         = { LED_PIN, false };
Button button      = { BTN_PIN, HIGH, 0, 0 };

AsyncWebServer server(HTTP_PORT);
AsyncWebSocket ws("/ws");

// ----------------------------------------------------------------------------
// SPIFFS initialization
// ----------------------------------------------------------------------------

void initSPIFFS() {
  if (!SPIFFS.begin()) {
    Serial.println("Cannot mount SPIFFS volume...");
    while (1) {
        onboard_led.on = millis() % 200 < 50;
        onboard_led.update();
    }
  }
}

// ----------------------------------------------------------------------------
// Connecting to the WiFi network
// ----------------------------------------------------------------------------

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Trying to connect [%s] ", WiFi.macAddress().c_str());
  while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      delay(500);
  }
  Serial.printf(" %s\n", WiFi.localIP().toString().c_str());
}

// ----------------------------------------------------------------------------
// Web server initialization
// ----------------------------------------------------------------------------

String processor(const String &var) {
    return String(var == "STATE" && led.on ? "on" : "off");
}

void onRootRequest(AsyncWebServerRequest *request) {
  request->send(SPIFFS, "/index.html", "text/html", false, processor);
}

void initWebServer() {
    server.on("/", onRootRequest);
    server.serveStatic("/", SPIFFS, "/");
    server.begin();
}


// ----------------------------------------------------------------------------
// Sending data to WebSocket clients
// ----------------------------------------------------------------------------

void notifyClients() {
    const uint8_t size = JSON_OBJECT_SIZE(1);
    StaticJsonDocument<size> json;
    json["status"] = led.on ? "on" : "off";

    char data[17];
    size_t len = serializeJson(json, data);
    ws.textAll(data, len);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {

        const uint8_t size = JSON_OBJECT_SIZE(1);
        StaticJsonDocument<size> json;
        DeserializationError err = deserializeJson(json, data);
        if (err) {
            Serial.print(F("deserializeJson() failed with code "));
            Serial.println(err.c_str());
            return;
        }

        const char *action = json["action"];
        if (strcmp(action, "toggle") == 0) {
            led.on = !led.on;
            notifyClients();
        }
        
    }
}

void onEvent(AsyncWebSocket       *server,  //
             AsyncWebSocketClient *client,  //
             AwsEventType          type,    // the signature of this function is defined
             void                 *arg,     // by the `AwsEventHandler` interface
             uint8_t              *data,    //
             size_t                len) {   //

    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("WebSocket client #%u disconnected\n", client->id());
            break;
        case WS_EVT_DATA:
            handleWebSocketMessage(arg, data, len);
            break;
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void initWebSocket() {
    ws.onEvent(onEvent);
    server.addHandler(&ws);
}


void getDeviceType(AsyncWebServerRequest *request){
    JsonDocument json;
    json["type"] = "1d";
    char data[17];
    serializeJson(json, data);
    request->send(200, "application/json", data);
}

void getNumberOfAxes(AsyncWebServerRequest *request){
    JsonDocument json;
    json["numberOfAxes"] = NUMBER_OF_AXES;
    char data[21];
    serializeJson(json, data);
    request->send(200, "application/json", data);
}

void getPosition(AsyncWebServerRequest *request){
    JsonDocument json;

    JsonArray axes = json["axes"].to<JsonArray>();
    axes.add(NUMBER_OF_AXES);

    JsonArray units = json["units"].to<JsonArray>();
    units.add("mm");

    JsonArray position = json["position"].to<JsonArray>();
    position.add(currentPosition);

    char data[128];
    serializeJson(json, data);
    request->send(200, "application/json", data);
}

void homeAxis(AsyncWebServerRequest *request){
    isHomed = true; //TODO actually home device
    request->send(200);
}

void axisHomeCheck(AsyncWebServerRequest *request){
    JsonDocument json;

    JsonArray axes = json["axesChecked"].to<JsonArray>();
    axes.add(NUMBER_OF_AXES);

    JsonArray status = json["homeStatus"].to<JsonArray>();
    status.add(isHomed);

    char data[128];
    serializeJson(json, data);
    request->send(200, "application/json", data);
}

void setPosition(AsyncWebServerRequest *request, JsonObject jsonObj){
    if (jsonObj.containsKey("position")) {
        float position = jsonObj["position"][0] ;
        currentPosition = position ;
    }
    request->send(200);
}

void getAxesLimits(AsyncWebServerRequest *request){
    JsonDocument json;

    JsonArray axes = json["axes"].to<JsonArray>();
    axes.add(NUMBER_OF_AXES);

    JsonArray limit = json["limits"].to<JsonArray>();
    limit.add(AXIS_LIMIT);

    JsonArray units = json["units"].to<JsonArray>();
    limit.add("mm");

    char data[128];
    serializeJson(json, data);
    request->send(200, "application/json", data);
}

// ----------------------------------------------------------------------------
// Initialization
// ----------------------------------------------------------------------------

void setup() {
    pinMode(onboard_led.pin, OUTPUT);
    pinMode(led.pin,         OUTPUT);
    pinMode(button.pin,      INPUT);

    Serial.begin(115200); delay(500);

    initSPIFFS();
    initWiFi();
    initWebSocket();
    initWebServer();
    server.on("/getDeviceType", HTTP_GET, [](AsyncWebServerRequest *request){ getDeviceType(request); });
    server.on("/getNumberOfAxes", HTTP_GET, [](AsyncWebServerRequest *request){ getNumberOfAxes(request); });
    server.on("/getPosition", HTTP_GET, [](AsyncWebServerRequest *request){ getPosition(request); });
    server.on("/homeAxis", HTTP_POST, [](AsyncWebServerRequest *request){ homeAxis(request); });
    server.on("/axisHomeCheck", HTTP_GET, [](AsyncWebServerRequest *request){ axisHomeCheck(request); });
    server.on("/getAxesLimits", HTTP_GET, [](AsyncWebServerRequest *request){ getAxesLimits(request); });

    AsyncCallbackJsonWebHandler* handler = new AsyncCallbackJsonWebHandler("/setPosition", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject jsonObj = json.as<JsonObject>();
        setPosition(request, jsonObj);
        //JsonDocument json;
    }); 
    server.addHandler(handler); 
}


// ----------------------------------------------------------------------------
// Main control loop
// ----------------------------------------------------------------------------

void loop() {
    ws.cleanupClients();
    button.read();

    if (button.pressed()) {
      led.on = !led.on;
      notifyClients();
    }
    
    onboard_led.on = millis() % 1000 < 50;

    led.update();
    onboard_led.update();
}