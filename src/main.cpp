#include <Arduino.h>
#include <M5Unified.h>
#include <TaskScheduler.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <UrlEncode.h>
#include <FastLED.h>

//ToDo: 
// * Token fetch based on valid until date --> use task scheduler for periodic refresh
// * Changed Status handling: During Timeplan set STATUS to RED even if the mower is in the charging station
// * Arrival of a pilot at the airfield: Pilot has to press the button at any time and not only if the mower is mowing

#define NUM_LEDS 25
#define DATA_PIN 27

// Define the array of leds
CRGB leds[NUM_LEDS];

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
void mainTaskCallback();
void GetToken();

Scheduler taskScheduler;

#define MAIN_TASK_DURATION 60000 // 60 seconds
#define TOKEN_TASK_DURATION 3600000*24 // 24 hours
Task mainTask(MAIN_TASK_DURATION, TASK_FOREVER, &mainTaskCallback);
Task tokenTask(TOKEN_TASK_DURATION, TASK_FOREVER, &GetToken);

bool isAirfieldBlocked = true;
bool isInTransition = false;
bool isScheduleCurrent = true;

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

// possible detection if zone or work is finished or not is the ration between
//     "taskTotalArea": 697,
//.    "taskCoverArea" : 297,

// maybe "scheduleCancel": 1, could be used to detect, if airfield is really free

// "scheduleInfoObject" : {
//   "start" : 28800,
//   "end" : 61200,
//   "yday" : 46101,
//   "type" : "future",
//   "day" : 4,
//   "pause" : false
// },

// "scheduleInfoObject": {
// "start" : 28800,
//     "end" : 61200,
//     "yday" : 46101,
//     "type" : "current",
//              "day" : 3,
//              "pause" : false
// }

// "scheduleInfoObject":
// {
//   "start" : 28800,
//       "end" : 61200,
//       "yday" : 46103,
//       "type" : "current",
//                "day" : 5,
//                "pause" : false
// }
// check if the mower is charching and TotalArea is not finished, if the cancel workplan will prevent the mower to start mowing again after it is fully charged.

void GetSettings()
{
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

    String scheduleType = doc["data"]["scheduleInfoObject"]["type"].as<String>();
    if (scheduleType == "current") {
      isScheduleCurrent = true;
    } else {
      isScheduleCurrent = false;
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

void SetActionStopTask() {
  SetAction("stop_task", "stopTask");
}

void ClearAirport() {
  SetActionStopTask();
  delay(2000);
  SetActionReturnToDock();
}

void FillDisplayWithColor(CRGB color) {
  FastLED.clear();
  for (int i = 0; i < NUM_LEDS; i++)
  {
    leds[i] = color;
  }
  FastLED.show();
}

void SetColorOutput() {
  FastLED.clear();
  if (isInTransition) {
    FillDisplayWithColor(CRGB::Blue);
    return;
  }
  
  if (isAirfieldBlocked) {
    FillDisplayWithColor(CRGB::Red);
  }
  else
  {
    FillDisplayWithColor(CRGB::Green);
  }
}

void GetStatus() {
  GetSettings();
  Serial.print("isAirfieldBlocked: ");
  Serial.print(isAirfieldBlocked);
  Serial.print(" isInTransition: ");
  Serial.println(isInTransition);
  Serial.print(" isScheduleCurrent: ");
  Serial.println(isScheduleCurrent);
}

void GetStatusAndUpdateColorOutput(){
  GetStatus();
  SetColorOutput();
}

void setup() {

  M5.begin();

  Serial.begin(115200);
  Serial.println("Serial initialized");

  // set explicitly the pin mode for the data pin to OUTPUT and set it LOW to avoid any flickering of the LEDs during boot
  pinMode(DATA_PIN, OUTPUT);
  digitalWrite(DATA_PIN, LOW);
  delay(1);

  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
  FastLED.setBrightness(50); // Helligkeit begrenzen
  FastLED.clear();

  // Connect to Wi-Fi
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


  // Get initial Token
  Serial.println("Get Token");
  while (accessToken == "")
  {
    GetToken();
    delay(1000);
  }
  Serial.print("Token: ");
  Serial.println(accessToken);

  // Init Scheduler and add Tasks
  taskScheduler.init();
  Serial.println("Initialized scheduler");
  taskScheduler.addTask(mainTask);
  Serial.println("added mainTask");
  mainTask.enable();
  taskScheduler.addTask(tokenTask);
  Serial.println("added tokenTask");
  tokenTask.enable();

  // Get Device ID and Serial Number once to be able to use it for all subsequent API calls
  Serial.println("Get Devices");
  while (deviceId == "" && deviceSerialNumber == "")
  {
    GetAllDevices();
    delay(1000);
    Serial.print(".");
  }
  Serial.print("Device ID: ");
  Serial.print(deviceId);
  Serial.print(" Device Serial Number: ");
  Serial.println(deviceSerialNumber);

  GetStatus();
  SetColorOutput();
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
