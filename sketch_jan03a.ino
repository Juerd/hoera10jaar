#include <Arduino.h>
#include <ArduinoOTA.h>
#define OTA_PASSWORD "bla"
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_task_wdt.h>

const char* ssid = "revspace-pub-2.4ghz";
const char* password = "";
const char* mqtt_server = "hoera10jaar.revspace.nl";

WiFiClient espClient;
PubSubClient client(espClient);

uint8_t row[] = { 27, 26, 32, 33, 25 };    // +
uint8_t col[] = { /* rood */ 4, 17, 18, /* groen */ 16, 5, 19 };  // n fet
int leds[30];
float brightness = .3;

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
  Serial.printf("Connecting to %s\n", ssid);
  WiFi.begin(ssid, password);
  wait_wifi();
}

void wait_wifi() {
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if (attempts++ > 5) {
      all(400, true, false, .1);
      all(400, false, false);
    } else {
      delay(100);
    }
    Serial.print(".");
  }

  randomSeed(micros());  // ?
  Serial.printf("\nIP address: %s\n", WiFi.localIP().toString().c_str());
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
    String clientId = "hoera10jaar-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe("hoera10jaar/+");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      all(5000, true, true, .1);
    }
  }
}


void setup() {
  for (int r = 0; r < sizeof(row); r++) pinMode(row[r], OUTPUT);  
  for (int c = 0; c < sizeof(col); c++) pinMode(col[c], OUTPUT);
  all(50, false, true, .1);
  all(0, false, false, .1);
  
  Serial.begin(115200);
  Serial.println("o hai");

  esp_task_wdt_init(30 /* seconds */, true);
  esp_err_t err = esp_task_wdt_add(NULL);
  Serial.println(err == ESP_OK ? "Watchdog ok" : "Watchdog fail");

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);  

  ArduinoOTA.setHostname("millennium");
  ArduinoOTA.setPassword(OTA_PASSWORD);
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
}
