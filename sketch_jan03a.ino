#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTP_Method.h>

#include <SPIFFS.h>

#define Sprintf(f, ...) ({ char* s; asprintf(&s, f, __VA_ARGS__); String r = s; free(s); r; })

const char* mqtt_server = "hoera10jaar.revspace.nl";

String me = "decennium-";

WiFiClient espClient;
PubSubClient client(espClient);

uint8_t row[] = { 27, 26, 32, 33, 25 };    // +
uint8_t col[] = { /* rood */ 4, 17, 18, /* groen */ 16, 5, 19 };  // n fet
int leds[30];
float brightness = .3;

const int button = 0;
void matrix() {
  for (int c = 0; c < sizeof(col); c++) {
    for (int r = 0; r < sizeof(row); r++) {
      digitalWrite(row[r], leds[c * 5 + r] );
    }
    digitalWrite(col[c], HIGH);
    delayMicroseconds(c >= 3 ? brightness*5 : brightness*10);
    digitalWrite(col[c], LOW);
    if (brightness < 0.2) {
      delayMicroseconds((.2 - brightness) * (c >= 3 ? 500 : 250));
    } else {
      delayMicroseconds(c >= 3 ? 10 : 5);
    }
  }  
  /*  
  for (int x = 0; x < 30; x++) {
    digitalWrite(col[x / 5], leds[x]);
    digitalWrite(row[x % 5], leds[x]);
    //delay(1);
    delayMicroseconds(x >= 15 ? 15 : 20);
    digitalWrite(col[x / 5], LOW);
    digitalWrite(row[x % 5], LOW);
  }
  */
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



void setup_wifi() {
  String ssid = SPIFFS.open("/wifi-ssid", "r").readString();
  String pw = SPIFFS.open("/wifi-password", "r").readString();
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
  String r = SPIFFS.open("/wifi-retry", "r").readString();
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

void setup_wifi_portal() {
  static WebServer http(80);  
  String wpa = SPIFFS.open("/wifi-portal-wpa", "r").readString();
  String ota = pwgen();
  
  if (wpa.length() && ota.length()) {
    WiFi.softAP(me.c_str(), ota.c_str());
  } else {
    WiFi.softAP(me.c_str());
  }
  delay(500);
  setup_ota();
  uint32_t _ip = WiFi.softAPIP();
  
  const String myip = Sprintf("%d.%d.%d.%d", (_ip & 0xff), (_ip >> 8 & 0xff), (_ip >> 16 & 0xff), (_ip >> 24));
  Serial.println(myip);
  
  http.on("/", HTTP_GET, []() {
    String ota = pwgen();
    
    int n = WiFi.scanNetworks();
    String html = "<!DOCTYPE html>\n<meta charset=UTF-8><title>" + me + "</title>"
    + "<form action=/restart method=post>Hi, I am " + me + ".<p>Currently configured SSID: %<br><input type=submit value=restart></form>"
    + "<hr><h2>Configure</h2><form method=post>SSID: <select name=ssid>";
    String current = SPIFFS.open("/wifi-ssid", "r").readString();
    if (current.length()) {
      current.replace("<", "&lt;");
      html.replace("%", current);
    } else {
      html.replace("%", "(not set)");
    }    
    for (int i = 0; i< n; i++) {
      String opt = "<option value='{value}'>{ssid}</option>";
      String ssid = WiFi.SSID(i);
      String value = "";
      for (int j = 0; j < ssid.length(); j++) {
        // hex encode to get byte-by-byte perfect representation
        value += Sprintf("&#%d;", ssid.charAt(j));
      }
      if (ssid == current) opt.replace("<option", "<option selected");
      ssid.replace("<", "&lt;");
      if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) ssid += " &#x1f512;";  // lock symbol
      if (WiFi.encryptionType(i) == WIFI_AUTH_WPA2_ENTERPRISE) ssid += " (won't work: 802.1x is not supported)";
      opt.replace("{ssid}", ssid);
      opt.replace("{value}", value);
      html += opt;
    }
    String retry = SPIFFS.open("/wifi-retry", "r").readString();
    String portalwpa = SPIFFS.open("/wifi-portal-wpa", "r").readString();

    html += "</select><br>Wifi WPA password: <input name=pw value=><br>"
      "<p>My own OTA/WPA password: <input name=ota value='{ota}' minlength=8 required> (8+ chars, you may want to save this somewhere, *now*)<br>"
      "<label><input type=checkbox name=portalpw value=yes{x}> Require &uarr;password&uarr; for this wifi configuration portal</label>"
      "<p><label><input type=radio name=retry value=no> Start this wifi configuration portal after wifi connection timeout</label><br>"
      "<label><input type=radio name=retry value=yes> Keep trying to connect to wifi (requires flashing firmware to change config)</label><br>"
      "<p><input type=submit></form>";
    html.replace("{x}", portalwpa.length() ? " checked" : "");
    String otahtml = "";
    for (int j = 0; j < ota.length(); j++) {
      // hex encode to get byte-by-byte perfect representation
      otahtml += Sprintf("&#%d;", ota.charAt(j));
    }
    html.replace("{ota}", otahtml);
    retry = retry.length() ? "=yes" : "=no";
    html.replace(retry, retry + " checked");
    http.send(200, "text/html", html);
  });
  http.on("/", HTTP_POST, []() {
    File s = SPIFFS.open("/wifi-ssid", "w");
    s.print(http.arg("ssid"));
    s.close();
    File p = SPIFFS.open("/wifi-password", "w");
    p.print(http.arg("pw"));
    p.close();
    File r = SPIFFS.open("/wifi-retry", "w");
    r.print(http.arg("retry") == "yes" ? "x" : "");
    r.close();
    File w = SPIFFS.open("/wifi-portal-wpa", "w");
    w.print(http.arg("portalpw") == "yes" ? "x" : "");
    w.close();
    File o = SPIFFS.open("/ota-password", "w");
    o.print(http.arg("ota"));
    o.close();
    http.send(200, "text/html", "<!DOCTYPE html><meta charset=UTF-8>\n<title>ok</title>Stored, maybe. <a href=/>Go see if it worked</a>");   

  });
  http.on("/restart", HTTP_POST, []() {
    http.send(200, "text/plain", "bye");
    delay(1000);
    ESP.restart();
  });
  http.begin();
  for (;;) {
    bool x = millis() % 1000 < 500;
    all(1, x, !x, .1);
    http.handleClient();
    ArduinoOTA.handle();
    esp_task_wdt_reset();
  }
  
}

void callback(char* topic, byte* payload, unsigned int length) {
  int lednr = -1;

  while (*topic != '/') topic++;
  topic++;

  if (     strcmp(topic, "leeuwarden") == 0) lednr = 0;
  else if (strcmp(topic, "amsterdam")  == 0) lednr = 1;
  else if (strcmp(topic, "utrecht")    == 0) lednr = 2;
  else if (strcmp(topic, "denhaag")    == 0) lednr = 3;
  else if (strcmp(topic, "rotterdam")  == 0) lednr = 4;
  else if (strcmp(topic, "zwolle")     == 0) lednr = 5;
  else if (strcmp(topic, "amersfoort") == 0) lednr = 6;
  else if (strcmp(topic, "arnhem")     == 0) lednr = 7;
  else if (strcmp(topic, "wageningen") == 0) lednr = 8;
  else if (strcmp(topic, "eindhoven")  == 0) lednr = 9;
  // 10 is niet aangesloten
  else if (strcmp(topic, "enschede")   == 0) lednr = 11;
  else if (strcmp(topic, "nijmegen")   == 0) lednr = 12;
  else if (strcmp(topic, "venlo")      == 0) lednr = 13;
  else if (strcmp(topic, "heerlen")    == 0) lednr = 14;
  
  if (lednr == -1) return;

  leds[lednr]      = length == 3 || length == 6;   // "red"   || "yellow"
  leds[15 + lednr] = length == 5 || length == 6;   // "green" || "yellow"
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT server");

    if (client.connect(me.c_str())) {
      Serial.println("connected");
      client.subscribe("hoera10jaar/+");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      all(5000, true, true, .1);
    }
  }
}


String pwgen() {
  const char* filename   = "/ota-password";
  const char* passchars  = "ABCEFGHJKLMNPRSTUXYZabcdefhkmnorstvxz23456789-#@%^<>";
  
  File pwfile = SPIFFS.open(filename, "r");
  String password = pwfile.readString();
  pwfile.close();

  if (password.length() == 0) {
    for (int i = 0; i < 16; i++) {
       password.concat( passchars[random(strlen(passchars))] );
    }
    File pwfile = SPIFFS.open(filename, "w");
    pwfile.print(password);
    pwfile.close();
  }
  
  return password;
}

void check_button() {
  if (digitalRead(button) == HIGH) return;
  unsigned long target = millis() + 1000;
  while (digitalRead(button) == LOW) {
    if (millis() > target) setup_wifi_portal();
  }
}

void setup() {
  for (int r = 0; r < sizeof(row); r++) pinMode(row[r], OUTPUT);  
  for (int c = 0; c < sizeof(col); c++) pinMode(col[c], OUTPUT);
  all(50, false, true, .1);
  all(0, false, false, .1);
  pinMode(button, INPUT);
  
  Serial.begin(115200);
  Serial.println("o hai");
  me += Sprintf("%12" PRIx64, ESP.getEfuseMac());
  Serial.println(me);

  esp_task_wdt_init(30 /* seconds */, true);
  esp_err_t err = esp_task_wdt_add(NULL);
  Serial.println(err == ESP_OK ? "Watchdog ok" : "Watchdog fail");

  SPIFFS.begin(true);

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void setup_ota() {
  
  String ota = pwgen();
  Serial.printf("OTA password is %s\n", ota.c_str());
  
  ArduinoOTA.setHostname(me.c_str());
  ArduinoOTA.setPassword(ota.c_str());
  ArduinoOTA.onStart([]() {
    all(1, true, true);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    float p = (float) progress / total;
    float maxled = p * 30.0;
    for (int i = 0; i < maxled; i++) {
      leds[i] = 0;
    }
    matrixdelay(2);
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

void loop() {
  static int i = 0;
  if (WiFi.status() != WL_CONNECTED) {
    // Werkt niet, status blijft WL_CONNECTED ook als AP weg is...
    // Dus hij blijft straks in de mqtt reconnect() hangen. Watchdog grijpt in.
    Serial.println("Reconnecting wifi...\n");
    WiFi.disconnect();
    setup_wifi();
  }
  
  if (WiFi.status() == WL_CONNECTED && !client.connected()) reconnect();
  
  if (i++ % 40 == 0) client.loop();
  
  matrix();

  ArduinoOTA.handle();
  esp_task_wdt_reset();
  check_button();
}
