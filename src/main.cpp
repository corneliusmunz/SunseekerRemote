#include <Arduino.h>
#include <M5Unified.h>
#include <TaskScheduler.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <UrlEncode.h>
#include <FastLED.h>

#define NUM_LEDS 25
#define DATA_PIN 27

// Define the array of leds
CRGB leds[NUM_LEDS];


void mainTaskCallback();

const char *WifiSsid = "";
const char *WifiPassword = "";
String username = "";
String password = "";
String baseUrl = "https://wirefree-specific.sk-robot.com/api/";
String accessToken = "";
String userId = "";
String deviceSerialNumber = "";
String deviceId = "";

// Scheduler
Scheduler taskScheduler;

#define DURATION 30000
Task t1(DURATION, TASK_FOREVER, &mainTaskCallback);

bool isAirfieldBlocked = false;
bool isInTransition = false;

HTTPClient http;

void GetToken() {
  http.begin(baseUrl + "auth/oauth/token");

  // Specify content-type header
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Authorization", "Basic YXBwOmFwcA==");

  // Data to send with HTTP POST
  String httpRequestData = "username="+urlEncode(username)+"&password=" + urlEncode(password)+"&grant_type=password&scope=server";

  // Send HTTP POST request
  int httpResponseCode = http.POST(httpRequestData);
  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    String response = http.getString();
    Serial.println(response);
    JsonDocument doc;
    deserializeJson(doc, response);
    const char *access_token = doc["access_token"];
    const char *refresh_token = doc["refresh_token"];
    int user_id = doc["user_id"];
    accessToken = String(access_token); 
    userId = String(user_id);
    Serial.print("Access Token: ");
    Serial.println(access_token);
    Serial.print("Refresh Token: ");
    Serial.println(refresh_token);
    Serial.print("User ID: ");
    Serial.println(user_id);
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
}

// property in json: data.workStatusCode
// #New apptype modes:
// #Unknown = 0,
// #Idle = 1,
// #Working = 2,
// #Pause = 3,
// #Error = 6,
// #Return = 7,
// #ReturnPause = 8,
// #Charging = 9,
// #ChargingFull = 10,
// #Offline = 13,
// #Locating = 15,
// #Stopp = 18

void GetSettings() {
  http.begin(baseUrl + "app_wireless_mower/device/info/" + deviceId);

  // Specify content-type header
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept-Language", "de");
  http.addHeader("Authorization", "Bearer " + accessToken);

  // Send HTTP GET request
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    String response = http.getString();
    JsonDocument doc;
    deserializeJson(doc, response);
    Serial.println(response);
    int workStatusCode = doc["data"]["workStatusCode"];
    if (workStatusCode == 9) {
      isInTransition = false;
      isAirfieldBlocked = false;
    } else if (workStatusCode == 10) {
      isInTransition = false;
      isAirfieldBlocked = false;
    }
    else if (workStatusCode == 2) {
      isInTransition = false;
      isAirfieldBlocked = true;
    } else {
      isInTransition = true;
    }
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
}

void GetAllDevices()
{
  http.begin(baseUrl + "app_wireless_mower/device-user/allDevice");

  // Specify content-type header
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept-Language", "de");
  http.addHeader("Authorization", "Bearer " + accessToken);

  // Send HTTP GET request
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0)
  {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    String response = http.getString();
    Serial.println(response);
    JsonDocument doc;
    deserializeJson(doc, response);
    const char *deviceIdChar = doc["data"][0]["deviceId"];
    deviceId = String(deviceIdChar);
    const char *deviceSerialNumberChar = doc["data"][0]["deviceSn"];
    deviceSerialNumber = String(deviceSerialNumberChar);
  }
  else
  {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
}

void SetAction(String cmd, String id)
{
  http.begin(baseUrl + "iot_mower/wireless/device/action");

  // Specify content-type header
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept-Language", "de");
  http.addHeader("Authorization", "Bearer " + accessToken);

  // Data to send with HTTP POST
  String httpRequestBody =
      "{\"method\" : \"action\",\"appId\" : " 
      + userId 
      + ",\"deviceSn\" : \"" 
      + deviceSerialNumber 
      + "\",\"cmd\" : \"" 
      + cmd 
      + "\",\"id\" : \"" 
      + id 
      + "\"}";
  Serial.print("httpRequestBody: ");
  Serial.println(httpRequestBody);

  // Send HTTP POST request
  int httpResponseCode = http.POST(httpRequestBody);
  if (httpResponseCode > 0)
  {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    String response = http.getString();
    Serial.println(response);
  }
  else
  {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
}

void SetActionStop(){
  SetAction("stop", "stopWork");
}

void SetActionStart(){
  SetAction("start", "startWork");
}

void SetActionReturnToDock(){
  SetAction("start_find_charger", "startFindCharger");
}

void SetActionPause(){
  SetAction("pause", "pauseWork");
}

void SetActionCancelWorkplan() {
  SetAction("cancel_time_tactics", "cancelTimeTactics");
}

void ClearAirport() {
  SetActionReturnToDock();
  delay(1000);
  SetActionCancelWorkplan();
}

void SetColorOutput() {
  FastLED.clear();
  if (isInTransition) {
    for (int i = 0; i < NUM_LEDS; i++)
    {
      leds[i] = CRGB::Blue;
    }
    FastLED.show();
    return;
  }
  
  if (isAirfieldBlocked) {
    for (int i = 0; i < NUM_LEDS; i++)
    {
      leds[i] = CRGB::Red;
    }
  }
  else
  {
    for (int i = 0; i < NUM_LEDS; i++)
    {
      leds[i] = CRGB::Green;
    }
  }
  FastLED.show();
}

void GetStatus() {
  GetToken();
  GetAllDevices();
  GetSettings();
  Serial.print("isAirfieldBlocked: ");
  Serial.print(isAirfieldBlocked);
  Serial.print(" isInTransition: ");
  Serial.println(isInTransition);
}

void GetStatusAndUpdateColorOutput(){
  GetStatus();
  SetColorOutput();
}

void setup() {

  M5.begin();

  Serial.begin(115200);
  Serial.println("Serial initialized");
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(50);
  FastLED.clear();

  WiFi.begin(WifiSsid, WifiPassword);
  Serial.println("Connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());
  GetStatus();
  SetColorOutput();

  taskScheduler.init();
  Serial.println("Initialized scheduler");
  taskScheduler.addTask(t1);
  Serial.println("added t1");
  t1.enable();
}

void checkButton(){
  if (M5.BtnA.wasClicked())
  {
    Serial.println("Button Clicked");
    isInTransition = true;
    SetColorOutput();

    if (isAirfieldBlocked)
    {
      ClearAirport();
    }
    else
    {
      SetActionStart();
    }
  }
}

void mainTaskCallback()
{
  GetStatusAndUpdateColorOutput();
}

void loop() {
  M5.update();
  taskScheduler.execute();
  checkButton();
}
