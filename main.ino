/*
  Final ESP-01 Relay — Multi-WiFi + Web UI + WiFiManager Portal + MQTT (LWT) + OTA
  - Portal reliably starts on web request; device reboots after saving
  - Shows last command (source: MQTT or WEB) on web UI
  - Non-blocking relay sequence (millis)
  - Accepts only UPPERCASE "ON" and "ON:<ms>"
  - STA hostname, mDNS, and OTA hostname set to "Smart_Sprayer"
*/

#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

/////////////////////
// EEPROM SETTINGS
/////////////////////
#define EEPROM_SIZE 1024
#define EEPROM_MAGIC 0x42
#define MAX_WIFI_SLOTS 6
#define SSID_LEN 32
#define PASS_LEN 32

struct Config {
  char wifi_ssid[MAX_WIFI_SLOTS][SSID_LEN];
  char wifi_pass[MAX_WIFI_SLOTS][PASS_LEN];
  char mqtt_server[40];
  char mqtt_user[32];
  char mqtt_pass[32];
  char mqtt_cmd_topic[40];
  char mqtt_status_topic[40];
  uint8_t magic;
};

Config config;

/////////////////////
// RELAY PIN
/////////////////////
const uint8_t RELAY_PIN = 2; // GPIO2 active LOW

WiFiClient espClient;
PubSubClient client(espClient);
ESP8266WebServer server(80);

/////////////////////
// RELAY STATE MACHINE (non-blocking)
/////////////////////
enum RelayState { RS_IDLE = 0, RS_PRESS1, RS_HOLD, RS_PRESS2 };
volatile RelayState relayState = RS_IDLE;
unsigned long relayStateTs = 0;
const unsigned long PRESS_DURATION = 250;
unsigned long holdDuration = 1000; // default hold time

/////////////////////
// TIMERS + STATUS
/////////////////////
unsigned long lastMqttAttempt = 0;
const unsigned long MQTT_RETRY_MS = 5000;
unsigned long wifiTryTimeout = 15000; // ms to try each saved WiFi

String lastCommand = "none";       // formatted like "MQTT:ON:3000" or "WEB:ON:1000"
unsigned long lastCommandTs = 0;

/////////////////////
// FORWARDS
/////////////////////
void saveConfig();
bool loadConfig();
void trySavedWiFis();
bool connectSingleWiFi(const char* ssid, const char* pass, unsigned long timeoutMs);
void startPortalAndSave();
void mqttReconnectIfNeeded();
void startRelaySequence(unsigned long requestedHoldMs);
void handleRoot();
void handleOn();
void handleConfig();
void handleRestart();

/////////////////////
// Save / Load Config
/////////////////////
void saveConfig() {
  config.magic = EEPROM_MAGIC;
  EEPROM.put(0, config);
  EEPROM.commit();
  Serial.println("Config saved to EEPROM");
}

bool loadConfig() {
  EEPROM.get(0, config);
  return config.magic == EEPROM_MAGIC;
}

/////////////////////
// Utility: safe strncpy with null termination
/////////////////////
void safeStrncpy(char* dest, const char* src, size_t maxlen) {
  if (!src || maxlen == 0) return;
  strncpy(dest, src, maxlen - 1);
  dest[maxlen - 1] = '\0';
}

/////////////////////
// Try to connect to one SSID (blocking up to timeoutMs)
/////////////////////
bool connectSingleWiFi(const char* ssid, const char* pass, unsigned long timeoutMs) {
  if (!ssid || strlen(ssid) == 0) return false;
  Serial.printf("Trying WiFi SSID: %s\n", ssid);
  WiFi.begin(ssid, pass);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\nFailed to connect to that SSID");
    return false;
  }
}

/////////////////////
// Try saved WiFis in order
/////////////////////
void trySavedWiFis() {
  WiFi.mode(WIFI_AP_STA); // enable STA + AP
  WiFi.softAP("Smart_Sprayer"); // start AP with fixed SSID
  Serial.println("AP started: Smart_Sprayer IP: 192.168.4.1");
  for (int i = 0; i < MAX_WIFI_SLOTS; ++i) {
    if (strlen(config.wifi_ssid[i]) == 0) continue;
    if (connectSingleWiFi(config.wifi_ssid[i], config.wifi_pass[i], wifiTryTimeout)) {
      return; // connected
    }
  }
  // none connected
}

/////////////////////
// Start WiFiManager portal and save values
/////////////////////
void startPortalAndSave() {
  WiFiManager wm;

  // Pre-fill MQTT fields with current config values
  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", config.mqtt_server, sizeof(config.mqtt_server));
  WiFiManagerParameter custom_mqtt_user("user", "MQTT User", config.mqtt_user, sizeof(config.mqtt_user));
  WiFiManagerParameter custom_mqtt_pass("pass", "MQTT Pass", config.mqtt_pass, sizeof(config.mqtt_pass));
  WiFiManagerParameter custom_mqtt_cmd("cmd", "MQTT Command Topic", config.mqtt_cmd_topic, sizeof(config.mqtt_cmd_topic));
  WiFiManagerParameter custom_mqtt_status("status", "MQTT Status Topic", config.mqtt_status_topic, sizeof(config.mqtt_status_topic));

  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);
  wm.addParameter(&custom_mqtt_cmd);
  wm.addParameter(&custom_mqtt_status);

  Serial.println("Starting config portal: Smart_Sprayer (forcing AP mode)");

  server.stop();
  delay(200);
  WiFi.disconnect(true);
  delay(500);
  WiFi.mode(WIFI_AP_STA);
  delay(500);

  if (!wm.startConfigPortal("Smart_Sprayer")) {
    Serial.println("Portal timed out or failed. Restarting...");
    delay(2000);
    ESP.restart();
    return;
  }

  // Save new WiFi into slot 0
  String newSsid = WiFi.SSID();
  String newPass = WiFi.psk();
  if (newSsid.length() > 0) {
    for (int i = MAX_WIFI_SLOTS - 1; i > 0; --i) {
      safeStrncpy(config.wifi_ssid[i], config.wifi_ssid[i - 1], SSID_LEN);
      safeStrncpy(config.wifi_pass[i], config.wifi_pass[i - 1], PASS_LEN);
    }
    safeStrncpy(config.wifi_ssid[0], newSsid.c_str(), SSID_LEN);
    safeStrncpy(config.wifi_pass[0], newPass.c_str(), PASS_LEN);
    Serial.printf("Saved new WiFi to slot 0: %s\n", config.wifi_ssid[0]);
  }

  // Save MQTT params
  safeStrncpy(config.mqtt_server, custom_mqtt_server.getValue(), sizeof(config.mqtt_server));
  safeStrncpy(config.mqtt_user, custom_mqtt_user.getValue(), sizeof(config.mqtt_user));
  safeStrncpy(config.mqtt_pass, custom_mqtt_pass.getValue(), sizeof(config.mqtt_pass));
  safeStrncpy(config.mqtt_cmd_topic, custom_mqtt_cmd.getValue(), sizeof(config.mqtt_cmd_topic));
  safeStrncpy(config.mqtt_status_topic, custom_mqtt_status.getValue(), sizeof(config.mqtt_status_topic));

  saveConfig();
  Serial.println("Configuration saved. Rebooting now...");
  delay(500);
  ESP.restart();
}

/////////////////////
// Start relay sequence (non-blocking)
/////////////////////
void startRelaySequence(unsigned long requestedHoldMs) {
  if (relayState != RS_IDLE) {
    Serial.println("Relay busy — ignoring new command.");
    return;
  }
  // enforce min/max limits
  if (requestedHoldMs < 250) requestedHoldMs = 250;         // minimum 250ms allowed
  const unsigned long MAX_HOLD_MS = 2UL * 60UL * 1000UL;    // 2 minutes max
  if (requestedHoldMs > MAX_HOLD_MS) requestedHoldMs = MAX_HOLD_MS;
  holdDuration = requestedHoldMs;
  digitalWrite(RELAY_PIN, LOW); // first press (active LOW)
  relayStateTs = millis();
  relayState = RS_PRESS1;
  Serial.printf("Relay sequence started. holdDuration=%lu ms\n", holdDuration);
}

/////////////////////
// MQTT callback
/////////////////////
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  Serial.print("Received ["); Serial.print(topic); Serial.print("]: "); Serial.println(msg);

  // Only accept uppercase "ON" or "ON:<ms>"
  if (msg.startsWith("ON")) {
    unsigned long requestedHold = 1000; 
    int colon = msg.indexOf(':');
    if (colon != -1) {
      String numPart = msg.substring(colon + 1);
      numPart.trim();
      unsigned long parsed = (unsigned long) numPart.toInt();
      if (parsed > 0) requestedHold = parsed;
    }

    // Cap requestedHold to avoid absurd values (e.g. 2 minutes max)
    const unsigned long MAX_HOLD_MS = 2UL * 60UL * 1000UL;
    if (requestedHold > MAX_HOLD_MS) {
      Serial.printf("Requested hold %lu ms exceeds max %lu ms — capping.\n", requestedHold, MAX_HOLD_MS);
      requestedHold = MAX_HOLD_MS;
      // optionally notify broker about capping
      if (client.connected()) client.publish(config.mqtt_status_topic, "cmd_capped", false);
    }

    lastCommand = "MQTT:" + msg;
    lastCommandTs = millis();
    startRelaySequence(requestedHold);
  } else {
    Serial.println("Ignored: only uppercase 'ON' or 'ON:<ms>' accepted.");
    // publish a helpful status so operators know their command was rejected
    if (client.connected()) {
      String rej = "rejected:";
      rej += msg;
      // Non-retained, small note on rejection
      client.publish(config.mqtt_status_topic, rej.c_str(), false);
    }
  }
}

/////////////////////
// MQTT reconnect
/////////////////////
void mqttReconnectIfNeeded() {
  if (client.connected()) return;
  if (millis() - lastMqttAttempt < MQTT_RETRY_MS) return;
  lastMqttAttempt = millis();

  Serial.print("Attempting MQTT connection to "); Serial.print(config.mqtt_server); Serial.println(" ...");
  String clientId = "ESP01_Relay_" + String(ESP.getChipId(), HEX);

  if (client.connect(clientId.c_str(),
                     config.mqtt_user, config.mqtt_pass,
                     config.mqtt_status_topic, 1, true, "offline")) {
    Serial.println("Connected to MQTT");
    client.subscribe(config.mqtt_cmd_topic);
    Serial.printf("Subscribed to topic: %s\n", config.mqtt_cmd_topic);
    client.publish(config.mqtt_status_topic, "online", true);
  } else {
    Serial.printf("MQTT connect failed, rc=%d\n", client.state());
  }
}

/////////////////////
// Web handlers
/////////////////////
String htmlHeader(const String &title = "Smart Sprayer") {
  String s = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'/>";
  s += "<title>" + title + "</title>";
  s += "<style>body{font-family:Arial,Helvetica,sans-serif;margin:12px;}button{padding:8px 12px;margin:6px;}input[type='number']{width:120px;padding:6px;margin-right:6px;} .info{background:#f3f3f3;padding:8px;border-radius:6px;margin-bottom:10px;}</style></head><body>";
  s += "<h2>" + title + "</h2>";
  return s;
}

String htmlFooter() {
  return "<p style='font-size:12px;color:#666;margin-top:12px;'>ESP-01 Sprayer Controller</p></body></html>";
}

void handleRoot() {
  String s = htmlHeader();

  s += "<div class='info'><strong>WiFi:</strong> ";
  if (WiFi.status() == WL_CONNECTED) {
    s += String(WiFi.SSID()) + " &nbsp; <strong>IP:</strong> " + WiFi.localIP().toString();
    s += " &nbsp; <strong>RSSI:</strong> " + String(WiFi.RSSI()) + " dBm";
  } else {
    s += "Not connected";
  }
  s += "<br><strong>MQTT:</strong> ";
  s += (client.connected() ? "Connected (OK)" : "Disconnected (OFF)");
  s += "<br><strong>MQTT Server:</strong> " + String(config.mqtt_server);
  s += "<br><strong>Command Topic:</strong> " + String(config.mqtt_cmd_topic);
  s += "<br><strong>Status Topic:</strong> " + String(config.mqtt_status_topic);
  s += "<br><strong>Last Command:</strong> " + lastCommand;
  if (lastCommandTs) s += " <small>(" + String((millis() - lastCommandTs)/1000) + "s ago)</small>";
  s += "</div>";

  s += "<form action='/on' method='POST' onsubmit='return validateMs()'>";
  s += "<button name='dur' value='1' type='submit'>Spray (1 second)</button><br><br>";
  s += "Set Custom Duration (milliseconds):<br>";
  s += "<input id='durms' type='number' name='durms' min='250' max='120000' placeholder='ms' value='1000' required />";
  s += "<button type='submit'>Spray</button>";
  s += "</form>";
  s += "<script>"
      "function validateMs(){"
      "var v=document.getElementById('durms').value;"
      "if(!v)return alert('Enter a value between 250 and 120000'),false;"
      "v=parseInt(v);"
      "if(v<250||v>120000){alert('Duration must be between 250 ms and 120 000 ms');return false;}"
      "return true;"
      "}"
      "</script>";


  s += "<p><a href='/config'>Open WiFi/MQTT Portal</a> &nbsp; | &nbsp; <a href='/restart'>Restart Device</a></p>";

  s += htmlFooter();
  server.send(200, "text/html", s);
}

void handleOn() {
  if (!server.hasArg("dur") && !server.hasArg("durms")) {
    server.send(400, "text/plain", "Missing parameter");
    return;
  }

  unsigned long ms = 1000; // default 1s

  if (server.hasArg("durms")) {
    String arg = server.arg("durms");
    Serial.printf("Received durms='%s'\n", arg.c_str());
    if (arg.length() > 0) {
      long parsed = arg.toInt();
      if (parsed > 0) ms = parsed;
    }
  } else if (server.hasArg("dur")) {
    String arg = server.arg("dur");
    Serial.printf("Received dur='%s'\n", arg.c_str());
    if (arg.length() > 0) {
      long parsed = arg.toInt();
      if (parsed > 0) ms = parsed * 1000;
    }
  }

  if (ms < 250) ms = 250;
  if (ms > 120000) ms = 120000;

  Serial.printf("Final web trigger duration: %lu ms\n", ms);

  lastCommand = "WEB:ON:" + String(ms);
  lastCommandTs = millis();
  startRelaySequence(ms);

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}



void handleConfig() {
  String msg = "Starting configuration portal.\nDevice will become an AP (Smart_Sprayer). Connect to that AP and configure WiFi/MQTT. The device will reconnect to your WiFi after saving and will reboot.";
  server.send(200, "text/plain", msg);
  server.client().flush();
  delay(200);
  startPortalAndSave();
}

void handleRestart() {
  server.send(200, "text/plain", "Restarting device...");
  delay(200);
  ESP.restart();
}

/////////////////////
// SETUP
/////////////////////
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // relay OFF

  WiFi.mode(WIFI_AP_STA); 

  server.on("/", HTTP_GET, handleRoot);
  server.on("/on", HTTP_POST, handleOn);
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/restart", HTTP_GET, handleRestart);
  server.begin();
  Serial.println("HTTP server started on AP: 192.168.4.1");

  EEPROM.begin(EEPROM_SIZE);
  if (!loadConfig()) {
    Serial.println("No valid config — initializing defaults");
    memset(&config, 0, sizeof(config));
    safeStrncpy(config.mqtt_server, "MQTT_Server", sizeof(config.mqtt_server));
    safeStrncpy(config.mqtt_user, "MQTT_User", sizeof(config.mqtt_user));
    safeStrncpy(config.mqtt_pass, "MQTT_Password", sizeof(config.mqtt_pass));
    safeStrncpy(config.mqtt_cmd_topic, "sprayer/button/cmd", sizeof(config.mqtt_cmd_topic));
    safeStrncpy(config.mqtt_status_topic, "sprayer/status", sizeof(config.mqtt_status_topic));
    config.magic = 0;
  }

  WiFi.hostname("Smart_Sprayer");
  trySavedWiFis();

  if (WiFi.status() == WL_CONNECTED) {
    // Set mDNS hostname
    if (MDNS.begin("Smart_Sprayer")) {
      Serial.println("mDNS responder started: http://Smart_Sprayer.local");
    } else {
      Serial.println("Error setting up mDNS responder!");
    }
  }

  client.setServer(config.mqtt_server, 1883);
  client.setCallback(callback);

  ArduinoOTA.setHostname("Smart_Sprayer");
  ArduinoOTA.begin();
}

/////////////////////
// LOOP
/////////////////////
void loop() {
  ArduinoOTA.handle();

  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastTry = 0;
    if (millis() - lastTry > 10000) {
      lastTry = millis();
      Serial.println("WiFi not connected — retrying saved networks...");
      trySavedWiFis();
    }
    server.handleClient();
    return;
  }

  mqttReconnectIfNeeded();
  if (client.connected()) client.loop();
  server.handleClient();

  unsigned long now = millis();
  switch (relayState) {
    case RS_IDLE: break;

    case RS_PRESS1:
      if (now - relayStateTs >= PRESS_DURATION) {
        digitalWrite(RELAY_PIN, HIGH);
        relayStateTs = now;
        relayState = RS_HOLD;
        Serial.println("Released after PRESS1 — now HOLD");
      }
      break;

    case RS_HOLD:
      if (now - relayStateTs >= holdDuration) {
        digitalWrite(RELAY_PIN, LOW);
        relayStateTs = now;
        relayState = RS_PRESS2;
        Serial.println("Second press (PRESS2)");
      }
      break;

    case RS_PRESS2:
      if (now - relayStateTs >= PRESS_DURATION) {
        digitalWrite(RELAY_PIN, HIGH);
        relayState = RS_IDLE;
        Serial.println("Relay sequence complete.");
      }
      break;
  }
}
