#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <SPIFFS.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTP_Method.h>
#include <esp_task_wdt.h>

#define Sprintf(f, ...) ({ char* s; asprintf(&s, f, __VA_ARGS__); String r = s; free(s); r; })

const char*  mqtt_server = "hoera10jaar.revspace.nl";
String       my_hostname = "decennium-";
const byte   row[] = { 27, 26, 32, 33, 25 };  // +
const byte   col[] = { /* rood */ 4, 17, 18, /* groen */ 16, 5, 19 };  // n fet
const int    button = 0;
int          leds[30];
float        brightness = .3;

WiFiClient   espClient;
PubSubClient mqtt(espClient);

//////// LED matrix

void matrix() {
  for (int c = 0; c < sizeof(col); c++) {
    bool any = false;
    for (int r = 0; r < sizeof(row); r++) {
      bool on = leds[c * 5 + r];
      if (on) any = true;
      digitalWrite(row[r], on);
    }
    digitalWrite(col[c], any);
    delayMicroseconds(c >= 3 ? brightness*5 : brightness*10);
    digitalWrite(col[c], LOW);
    if (brightness < 0.2) {
      delayMicroseconds((.2 - brightness) * (c >= 3 ? 500 : 250));
    } else {
      delayMicroseconds(c >= 3 ? 10 : 5);
    }
  }
}

void matrixdelay(int ms) {
  unsigned long end = millis() + ms;
  while(millis() < end) matrix();
}

void all(int ms, bool red, bool green, float b = 1) {
  float oldb = brightness;
  brightness = b;
  for (int i = 0; i < 15; i++) leds[i] = red;
  for (int i = 15; i < 30; i++) leds[i] = green;
  matrixdelay(ms);
  brightness = oldb;
}

//////// Over-The-Air update

void setup_ota() {
  String ota = pwgen();
  Serial.printf("OTA password is %s\n", ota.c_str());

  ArduinoOTA.setHostname(my_hostname.c_str());
  ArduinoOTA.setPassword(ota.c_str());
  ArduinoOTA.onStart([]() {
    all(1, true, false);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    float p = (float) progress / total;
    float maxled = p * 30.0;
    if (maxled >= 14.5 && maxled < 15.0) all(0, false, true);
    for (int i = 0; i < maxled; i++) {
      leds[i] = 0;
    }
    matrixdelay(3);
    esp_task_wdt_reset();
  });
  ArduinoOTA.onError([](ota_error_t error) {
    all(5000, true, false);
  });
  ArduinoOTA.onEnd([]() {
    all(2000, false, true);
  });

  ArduinoOTA.begin();
}

void check_button() {
  if (digitalRead(button) == HIGH) return;
  unsigned long target = millis() + 1000;
  while (digitalRead(button) == LOW) {
    if (millis() > target) setup_wifi_portal();
  }
}

//////// end-user configurable

String read(const char* fn) {
  File f = SPIFFS.open(fn, "r");
  String r = f.readString();
  f.close();
  return r;
}

void store(const char* fn, String content) {
  File f = SPIFFS.open(fn, "w");
  f.print(content);
  f.close();
}

String pwgen() {
  const char* filename   = "/ota-password";
  const char* passchars  = "ABCEFGHJKLMNPRSTUXYZabcdefhkmnorstvxz23456789-#@%^<>";

  String password = read(filename);
  
  if (password.length() == 0) {
    for (int i = 0; i < 16; i++) {
       password.concat( passchars[random(strlen(passchars))] );
    }
    store(filename, password);
  }

  return password;
}

String html_entities(String raw) {
  String r;
  for (int i = 0; i < raw.length(); i++) {
    char c = raw.charAt(i);
    if (c >= '!' && c <= 'z' && c != '&' && c != '<' && c != '>') {
      // printable ascii minus html and {}
      r += c;
    } else {
      r += Sprintf("&#%d;", raw.charAt(i));
    }
  }
  return r;
}

void setup_wifi_portal() {
  static WebServer http(80);
  static DNSServer dns;
  static int num_networks = -1;
  String wpa = read("/wifi-portal-wpa");
  String ota = pwgen();
  
  if (wpa.length() && ota.length()) {
    WiFi.softAP(my_hostname.c_str(), ota.c_str());
  } else {
    WiFi.softAP(my_hostname.c_str());
  }
  dns.setTTL(0);
  dns.start(53, "*", WiFi.softAPIP());
  setup_ota();

  Serial.println(WiFi.softAPIP().toString());

  http.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html>\n<meta charset=UTF-8>"
      "<title>{hostname}</title>"
      "<form action=/restart method=post>"
        "Hi, I am {hostname}."
        "<p>Currently configured SSID: {ssid}<br>"
        "<input type=submit value=restart>"
      "</form>"
      "<hr>"
      "<h2>Configure</h2>"
      "<form method=post>"
        "SSID: <select name=ssid>{options}</select> <a href=/rescan>rescan</a>"
        "</select><br>Wifi WPA password: <input name=pw value=''><br>"
        "<p>My own OTA/WPA password: <input name=ota value='{ota}' minlength=8 required> (8+ chars, you may want to save this somewhere, *now*)<br>"
        "<label><input type=checkbox name=portalpw value=yes{portalwpa}> Require &uarr;password&uarr; for this wifi configuration portal</label>"
        "<p><label><input type=radio name=retry value=no{retry-no}> Start this wifi configuration portal after wifi connection timeout</label><br>"
        "<label><input type=radio name=retry value=yes{retry-yes}> Keep trying to connect to wifi (requires flashing firmware to change config)</label><br>"
        "<p><input type=submit>"
      "</form>";

    String current = read("/wifi-ssid");
    
    html.replace("{hostname}",  my_hostname);
    html.replace("{ssid}",      current.length() ? html_entities(current) : "(not set)");
    html.replace("{portalwpa}", read("/wifi-portal-wpa").length() ? " checked" : "");
    html.replace("{ota}",       html_entities(pwgen()));
    
    bool r = read("/wifi-retry").length();
    html.replace("{retry-yes}", r ? " checked" : "");
    html.replace("{retry-no}",  r ? "" : " checked");
    
    String options;
    if (num_networks < 0) num_networks = WiFi.scanNetworks();
    bool found = false;
    for (int i = 0; i < num_networks; i++) {
      String opt = "<option value='{ssid}'{sel}>{ssid} {lock} {1x}</option>";
      String ssid = WiFi.SSID(i);
      wifi_auth_mode_t mode = WiFi.encryptionType(i);

      opt.replace("{sel}",  ssid == current && !(found++) ? " selected" : "");
      opt.replace("{ssid}", html_entities(ssid));
      opt.replace("{lock}", mode != WIFI_AUTH_OPEN ? "&#x1f512;" : "");
      opt.replace("{1x}",   mode == WIFI_AUTH_WPA2_ENTERPRISE ? "(won't work: 802.1x is not supported)" : "");
      options += opt;
    }
    html.replace("{options}", options);
    http.send(200, "text/html", html);
  });
  
  http.on("/", HTTP_POST, []() {
    store("/wifi-ssid",       http.arg("ssid"));
    store("/wifi-password",   http.arg("password"));
    store("/wifi-retry",      http.arg("retry") == "yes" ? "x" : "");
    store("/wifi-portal-wpa", http.arg("portalpw") == "yes" ? "x" : "");
    store("/ota-password",    http.arg("ota"));
    http.sendHeader("Location", "/");
    http.send(302, "text/plain", "ok");
  });
  
  http.on("/restart", HTTP_POST, []() {
    http.send(200, "text/plain", "bye");
    delay(1000);
    ESP.restart();
  });

  http.on("/rescan", HTTP_GET, []() {
    http.sendHeader("Location", "/");
    http.send(302, "text/plain", "wait for it...");
    num_networks = WiFi.scanNetworks();
  });

  http.onNotFound([]() {
    http.sendHeader("Location", "http://" + my_hostname + "/");
    http.send(302, "text/plain", "hi");
  });
  
  http.begin();
  
  for (;;) {
    bool x = millis() % 1000 < 333;
    bool y = millis() % 1000 > 666;
    all(1, x || !y, y || !x, .1);
    http.handleClient();
    dns.processNextRequest();
    ArduinoOTA.handle();
    esp_task_wdt_reset();
  }
}

//////// Wifi + mqtt client

void setup_wifi() {
  String ssid = read("/wifi-ssid");
  String pw = read("/wifi-password");
  if (ssid.length() == 0) {
    Serial.println("First contact!\n");
    setup_wifi_portal();
  }
  Serial.printf("Connecting to %s\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), pw.c_str());
  setup_ota();
  wait_wifi();
}

void wait_wifi() {
  int attempts = 0;
  String r = read("/wifi-retry");
  bool retry = r.length();
  while (WiFi.status() != WL_CONNECTED) {
    if (attempts++ > 5) {
      all(500, true, false, .1);
      check_button();
      all(500, false, false);
      check_button();
      esp_task_wdt_reset();
    } else {
      delay(100);
    }
    if (attempts > 30 && !retry) {
      Serial.println("Giving up. Starting config portal.");
      setup_wifi_portal();
    }
    Serial.println(attempts);
  }

  randomSeed(micros());  // ?
  Serial.printf("\nIP address: %s\n", WiFi.localIP().toString().c_str());
}

void callback(char* topic, byte* message, unsigned int length) {
  String t = topic;
  t.replace("hoera10jaar/", "");

  int lednr = -1;
  if (     t == "leeuwarden") lednr = 0;
  else if (t == "amsterdam")  lednr = 1;
  else if (t == "utrecht")    lednr = 2;
  else if (t == "denhaag")    lednr = 3;
  else if (t == "rotterdam")  lednr = 4;
  else if (t == "zwolle")     lednr = 5;
  else if (t == "amersfoort") lednr = 6;
  else if (t == "arnhem")     lednr = 7;
  else if (t == "wageningen") lednr = 8;
  else if (t == "eindhoven")  lednr = 9;
  // 10 is niet aangesloten
  else if (t == "enschede")   lednr = 11;
  else if (t == "nijmegen")   lednr = 12;
  else if (t == "venlo")      lednr = 13;
  else if (t == "heerlen")    lednr = 14;
  
  if (lednr == -1) return;

  char* m = (char*) message;
  if (strncmp(m, "red", length) == 0) {
    leds[lednr]      = 1;
    leds[lednr + 15] = 0;
  } else if (strncmp(m, "green", length) == 0) {
    leds[lednr]      = 0;
    leds[lednr + 15] = 1;
  } else if (!length) {
    leds[lednr]      = 0;
    leds[lednr + 15] = 0;
  } else {  // "yellow" waarschijnlijk
    leds[lednr]      = 1;
    leds[lednr + 15] = 1;
  }
}

void reconnect_mqtt() {
  // Als de wifi weg is, blijft dit hangen en grijpt de watchdog in.
  // WiFi.status() blijft echter op WL_CONNECTED dus slimmere afhandeling is lastig.

  while (!mqtt.connected()) {
    Serial.print("Connecting to MQTT server");

    if (mqtt.connect(my_hostname.c_str())) {
      Serial.println("connected");
      mqtt.subscribe("hoera10jaar/+");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt.state());
      all(5000, true, true, .1);
    }
  }
}

//////// Main

void setup() {
  for (int r = 0; r < sizeof(row); r++) pinMode(row[r], OUTPUT);  
  for (int c = 0; c < sizeof(col); c++) pinMode(col[c], OUTPUT);
  all(50, true, true, .1);
  all(0, false, false, .1);
  pinMode(button, INPUT);

  Serial.begin(115200);
  Serial.println("o hai");
  my_hostname += Sprintf("%12" PRIx64, ESP.getEfuseMac());
  Serial.println(my_hostname);

  esp_task_wdt_init(30 /* seconds */, true);
  esp_err_t err = esp_task_wdt_add(NULL);
  Serial.println(err == ESP_OK ? "Watchdog ok" : "Watchdog fail");

  SPIFFS.begin(true);

  setup_wifi();
  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCallback(callback);
}


void loop() {
  if (!mqtt.connected()) reconnect_mqtt();

  // Traag, dus niet te vaak doen ivm matrix refresh rate
  static unsigned int i = 0;
  if (i++ % 40 == 0) mqtt.loop();

  matrix();
  ArduinoOTA.handle();
  esp_task_wdt_reset();
  check_button();
}
