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
const int    numrows = 5;
const int    numcols = 6;
const int    rows[numrows] = { 27, 26, 32, 33, 25 };  // +
const int    cols[numcols] = { /* rood */ 4, 17, 18, /* groen */ 16, 5, 19 };  // n fet

const int    button = 0;
const int    numleds = 30;
int          leds[numleds];
int          current[numleds];
const int    OFF = 0;
const int    ON_R = 84;
const int    ON_G = 64;
const int    ON_YR = 84;
const int    ON_YG = 30;
int          fade_interval = 5;
int          waitstep = 10;
int          wait = -14 * waitstep;


WiFiClient   espClient;
PubSubClient mqtt(espClient);

//////// LED matrix


// Highly optimized and tweaked
void loop() {  // Pinned to core 1, nothing else is.
  const static int levels[] = { 192,40,224,8,24,152,128,104,184,16,88,216,208,240,
  176,232,200,32,160,72,248,80,56,112,136,0,48,144,168,64,96,120};  // ((0..31) »*» 8).pick(*).join(",")
  const uint32_t colgpio[numcols] = {  // calculate at compile time
    (uint32_t)1 << cols[0],
    (uint32_t)1 << cols[1],
    (uint32_t)1 << cols[2],
    (uint32_t)1 << cols[3],
    (uint32_t)1 << cols[4],
    (uint32_t)1 << cols[5]
  };
  const static uint32_t rowgpio[numcols] = {
    rows[0] < 32 ? ((uint32_t)1 << rows[0]) : ((uint32_t)1 << (rows[0] - 32)),
    rows[1] < 32 ? ((uint32_t)1 << rows[1]) : ((uint32_t)1 << (rows[1] - 32)),
    rows[2] < 32 ? ((uint32_t)1 << rows[2]) : ((uint32_t)1 << (rows[2] - 32)),
    rows[3] < 32 ? ((uint32_t)1 << rows[3]) : ((uint32_t)1 << (rows[3] - 32)),
    rows[4] < 32 ? ((uint32_t)1 << rows[4]) : ((uint32_t)1 << (rows[4] - 32)),
  };
  const static int rowregister[numcols] = {
    rows[0] >= 32,
    rows[1] >= 32,
    rows[2] >= 32,
    rows[3] >= 32,
    rows[4] >= 32,
  };

  for (;;) {  // Never hand back control
    //static int x = 0;
    //unsigned long start = micros();
    // int is faster than uint_fast8_t?!
    for (int s = 0; s < 32; s++) {
      for (int c = 0; c < numcols; c++) {
        bool any = false;
        for (int r = numrows - 1; r >= 0; r--) {
            bool on = current[c * 5 + r] > levels[s];

            // digitalWrite(rows[r], on) unrolled:
            if (on) {
                any = true;
                if (rowregister[r]) GPIO.out1_w1ts.val = rowgpio[r];
                else                GPIO.out_w1ts      = rowgpio[r];
            } else {
                if (rowregister[r]) GPIO.out1_w1tc.val = rowgpio[r];
                else                GPIO.out_w1tc      = rowgpio[r];
            }
        }
        if (any) GPIO.out_w1ts = colgpio[c];  // digitalWrite(cols[c], HIGH);
        else     GPIO.out_w1tc = colgpio[c];  // digitalWrite(cols[c], LOW);

        ets_delay_us(2);  // more stable than delayMicros()
        GPIO.out_w1tc = colgpio[c];  // digitalWrite(cols[c], LOW);
        //ets_delay_us(1);
      }
    }
    esp_task_wdt_reset();
    //if (x++ % 10000 == 0) Serial.println(micros() - start);
  }
}

void all(int ms, bool red, bool green) {
  unsigned long start = millis();
  for (int i =  0; i < 15; i++) leds[i] = red * (green ? ON_YR : ON_R);
  for (int i = 15; i < 30; i++) leds[i] = green * (red ? ON_YG : ON_G);
  if (ms) {
    wait_fade();
    while (millis() - start < ms) delay(1);
  }
}

bool fade() {
  static unsigned long previous = 0;
  if (!previous || (millis() - previous > fade_interval)) {
    previous = millis();
  } else {
    return true;
  }

  bool faded = false;
  for (int i = 0; i < 30; i++) {
    if (leds[i] > current[i]) { current[i]++; faded = true; }
    else if (leds[i] < current[i]) { current[i]--; faded = true; }
  }
  return faded;
}

void wait_fade() {
  while (fade());
}

//////// Over-The-Air update

void setup_ota() {
  String ota = pwgen();
  Serial.printf("OTA-wachtwoord is %s\n", ota.c_str());

  ArduinoOTA.setHostname(my_hostname.c_str());
  ArduinoOTA.setPassword(ota.c_str());
  ArduinoOTA.onStart([]() {
    fade_interval = 1;
    all(0, true, false);
    wait_fade();
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    float p = (float) progress / total;
    float maxled = p * 30.0;
    if (maxled >= 14.5 && maxled < 15.0) { all(0, false, true); wait_fade(); }
    for (int i = 0; i < maxled; i++) current[i] = leds[i] = 0;
    esp_task_wdt_reset();
  });
  ArduinoOTA.onError([](ota_error_t error) {
    all(5000, true, false);
    all(1, false, false);
  });
  ArduinoOTA.onEnd([]() {
    all(2000, false, true);
    all(1, false, false);
  });

  ArduinoOTA.begin();
}

void check_button() {
  if (digitalRead(button) == HIGH) return;
  unsigned long start = millis();
  while (digitalRead(button) == LOW) {
    if (millis() - start > 1000) setup_wifi_portal();
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

  WiFi.disconnect();

  if (wpa.length() && ota.length()) {
    WiFi.softAP(my_hostname.c_str(), ota.c_str());
  } else {
    WiFi.softAP(my_hostname.c_str());
  }
  delay(500);
  dns.setTTL(0);
  dns.start(53, "*", WiFi.softAPIP());
  setup_ota();

  Serial.println(WiFi.softAPIP().toString());

  http.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html>\n<meta charset=UTF-8>"
      "<title>{hostname}</title>"
      "<form action=/restart method=post>"
        "Hallo, ik ben {hostname}."
        "<p>Huidig ingestelde SSID: {ssid}<br>"
        "<input type=submit value='Opnieuw starten'>"
      "</form>"
      "<hr>"
      "<h2>Configure</h2>"
      "<form method=post>"
        "SSID: <select name=ssid onchange=\"document.getElementsByName('password')[0].value=''\">{options}</select> "
        "<a href=/rescan onclick=\"this.innerHTML='scant...';\">opnieuw scannen</a>"
        "</select><br>Wifi WEP/WPA-wachtwoord: <input name=password value='{password}'><br>"
        "<p>Mijn eigen OTA/WPA-wachtwoord: <input name=ota value='{ota}' minlength=8 required> (8+ tekens, en je wilt deze waarschijnlijk *nu* ergens opslaan)<br>"
        "<label><input type=checkbox name=portalpw value=yes{portalwpa}> &uarr;Wachtwoord&uarr; vereisen voor deze wifi-configuratieportaal</label>"
        "<p><label><input type=radio name=retry value=no{retry-no}> Wifi-configuratieportaal starten als verbinding met wifi faalt./label><br>"
        "<label><input type=radio name=retry value=yes{retry-yes}> Blijven proberen te verbinden met wifi (hou Flash ingedrukt om hier terug te komen)</label><br>"
        "<p><input type=submit value=Opslaan>"
      "</form>";

    String current = read("/wifi-ssid");
    String pw = read("/wifi-password");
    
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
    html.replace("{password}", found && pw.length() ? "##**##**##**" : "");
    html.replace("{options}",  options);
    http.send(200, "text/html", html);
  });
  
  http.on("/", HTTP_POST, []() {
    String pw = http.arg("password");
    if (pw != "##**##**##**")
        store("/wifi-password", pw);
    store("/wifi-ssid",         http.arg("ssid"));
    store("/wifi-retry",        http.arg("retry") == "yes" ? "x" : "");
    store("/wifi-portal-wpa",   http.arg("portalpw") == "yes" ? "x" : "");
    store("/ota-password",      http.arg("ota"));
    http.sendHeader("Location", "/");
    http.send(302, "text/plain", "ok");
  });
  
  http.on("/restart", HTTP_POST, []() {
    http.send(200, "text/plain", "Doei!");
    all(1000, false, false);
    ESP.restart();
  });

  http.on("/rescan", HTTP_GET, []() {
    http.sendHeader("Location", "/");
    http.send(302, "text/plain", "wait for it...");
    num_networks = WiFi.scanNetworks();
  });

  http.onNotFound([]() {
    http.sendHeader("Location", "http://" + my_hostname + "/");
    http.send(302, "text/plain", "hoi");
  });
  
  http.begin();
 
  fade_interval = 3;
  for (;;) {
    unsigned long m = millis();
    if (m % 1000 < 20) {
      bool x = m % 2000 < 1000;
      all(0, x, !x);
    }
    fade();
    http.handleClient();
    dns.processNextRequest();
    ArduinoOTA.handle();
    esp_task_wdt_reset();
    vTaskDelay(1);
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
  Serial.printf("Verbinden met %s...\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), pw.c_str());
  setup_ota();
  wait_wifi();
}

void wait_wifi() {
  int attempts = 0;
  String r = read("/wifi-retry");
  bool retry = r.length();
  bool do_yay = false;
  while (WiFi.status() != WL_CONNECTED) {
    if (attempts++ > 2) {
      do_yay = true;
      all(500, true, false);
      check_button();
      all(500, false, false);
      check_button();
      esp_task_wdt_reset();
      vTaskDelay(1);
    } else {
      delay(1000);
      check_button();
    }
    if (attempts > 30 && !retry) {
      Serial.println("Ik geef het op.");
      setup_wifi_portal();
    }
    Serial.println(attempts);
  }

  if (do_yay) {
    all(1, false, true);
    all(1, false, false);
  }
  randomSeed(micros());  // ?
  Serial.printf("\nIP-adres: %s\n", WiFi.localIP().toString().c_str());
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
  int minus = 0;
  if (!leds[lednr] && !leds[lednr+15] && wait < 0) { minus = wait; wait += waitstep; }

  char* m = (char*) message;
  if (strncmp(m, "red", length) == 0) {
    leds[lednr]      = ON_R;
    leds[lednr + 15] = OFF;
  } else if (strncmp(m, "green", length) == 0) {
    leds[lednr]      = OFF;
    leds[lednr + 15] = ON_G;
  } else if (length <= 1) {
    leds[lednr]      = OFF;
    leds[lednr + 15] = OFF;
  } else {  // "yellow" waarschijnlijk
    leds[lednr]      = ON_YR;
    leds[lednr + 15] = ON_YG;
  }
  current[lednr] += minus;
  current[lednr + 15] += minus;
}

void reconnect_mqtt() {
  // Als de wifi weg is, blijft dit hangen en grijpt de watchdog in.
  // WiFi.status() blijft echter op WL_CONNECTED dus slimmere afhandeling is lastig.

  while (!mqtt.connected()) {
    Serial.print("Verbinden met MQTT-server...");

    if (mqtt.connect(my_hostname.c_str())) {
      Serial.println("verbonden");
      mqtt.subscribe("hoera10jaar/+");
    } else {
      Serial.printf("mislukt, rc=%d\n", mqtt.state());
      all(5000, true, true);
      all(1, false, false);
    }
  }
}

//////// Main

void setup() {
  for (int r = 0; r < numrows; r++) pinMode(rows[r], OUTPUT);  
  for (int c = 0; c < numcols; c++) pinMode(cols[c], OUTPUT);
  pinMode(button, INPUT);

  Serial.begin(115200);
  Serial.println("c hai");
  my_hostname += Sprintf("%12" PRIx64, ESP.getEfuseMac());
  Serial.println(my_hostname);

  xTaskCreatePinnedToCore(
    network,      /* Function to implement the task */
    "network",    /* Name of the task */
    4096,         /* Stack size in words */
    NULL,         /* Task input parameter */
    3,            /* Priority of the task */
    NULL,         /* Task handle. */
    0             /* Core where the task should run */
  );
}

void network(void * pvParameters) {
  esp_task_wdt_init(30 /* seconds */, true);
  esp_err_t err = esp_task_wdt_add(NULL);
  Serial.println(err == ESP_OK ? "Watchdog ok" : "Watchdog faal");

  SPIFFS.begin(true);
  setup_wifi();
  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCallback(callback);

  while (1) {
    if (!mqtt.connected()) reconnect_mqtt();
    fade();

    mqtt.loop();
    ArduinoOTA.handle();
    check_button();

    esp_task_wdt_reset();
    vTaskDelay(1);
  }
}
