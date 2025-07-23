// ESP32 MQTT + Dual Relay Control with Availability and HA Discovery
#define MQTT_MAX_PACKET_SIZE 1024

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include <Preferences.h>
#include <esp_system.h>
#include <HTTPClient.h>        // >>> OTA
#include <ArduinoJson.h>       // >>> OTA
#include <Update.h>            // >>> OTA
#include <WiFiClientSecureAxTLS.h>  // или <WiFiClientSecure.h>, если доступен
#include <ArduinoOTA.h>


#define DEF_WIFI_SSID "SmartGrow_AP"
#define DEF_WIFI_PASS "smartgrow_xoqO7h7o"

#define DEF_MQTT_SERVER "192.168.1.100"
#define DEF_MQTT_USER "sg_mqtt_user"
#define DEF_MQTT_PASS "858hBCnYAUv73fa54hSf"

// ====== Параметры устройства ======
#define DEVICE_NAME_BASE "BT Box"
#define DEVICE_MODEL "BT_BoxKit.v02"
#define FW_VERSION "1.0.5"  // >>> OTA: текущая версия
const char* manufacturer = "SmartGrow LLC";
const char* UPDATE_JSON =
  "https://ota.sg.if.ua/firmware/blacktower/box/latest.json";  // >>> OTA: URL манифеста

// ====== Пины ======
const int CONFIG_BUTTON_PIN = 2;  // GPIO2 → GND для портала
const int RELAY1_PIN = 17;        // GPIO16 → IN1 реле-модуляF
const int RELAY2_PIN = 16;        // GPIO17 → IN2 реле-модуля

// ====== Пины RGB-LED (common-cathode) ======
const uint8_t PIN_R = 14;
const uint8_t PIN_G = 12;
const uint8_t PIN_B = 13;


// ====== NVS для MQTT ======
Preferences prefs;
Preferences versionPrefs;
String pendingVersion;
// >>> OTA ----------------------------------------------------------------
WiFiClientSecure tlsClient;  // HTTPS клиент для прошивки
String otaStateTopic, otaCmdTopic, otaConfigTopic;
String pendingURL;
uint32_t lastOtaCheck = 0;



// ------------------------------------------------------------------------

// -------- LED breathing while OTA --------------------------------------
bool otaInProgress = false;    // поднимается в otaInstall()
uint8_t breathLevel = 0;       // текущая яркость 0–255
int8_t breathDir = 1;          // +1 / -1
unsigned long lastBreath = 0;  // таймер для 1 шага fade
// ------------------------------------------------------------------------

// ====== Клиенты Wi-Fi и MQTT ======
WiFiManager wm;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ====== Буферы для MQTT-параметров ======
char mqtt_server[64] = "";
char mqtt_user[32] = "";
char mqtt_pass[32] = "";
WiFiManagerParameter paramServer("mqtt_server", "MQTT Server", mqtt_server, sizeof(mqtt_server));
WiFiManagerParameter paramUser("mqtt_user", "MQTT User", mqtt_user, sizeof(mqtt_user));
WiFiManagerParameter paramPass("mqtt_pass", "MQTT Pass", mqtt_pass, sizeof(mqtt_pass));

// ====== Идентификатор и топики ======
char device_id[20];
char device_name[32];
String baseTopic;
String availabilityTopic;
String relay1CmdTopic, relay1StateTopic;
String relay2CmdTopic, relay2StateTopic;

// ====== Прототипы ======
void generateDeviceID();
void openPortalAndRestart();  //добавить
void setupTopics();
void updateStatusLED();
void loadMQTTSettings();
void saveMQTTSettings();
bool connectWiFi(unsigned long timeout, bool tryDefault = true);
bool connectMQTT(unsigned long timeout);
void startConfigPortal();
void publishAvailability();
void mqttCallback(char* topic, byte* payload, unsigned int len);
void controlRelay(int pin, const String& cmd);


#define LEN_B       37
#define B_CUR_TMP   13
#define B_CMD       17
#define B_MODE      23
#define B_FAN_SPD   25
#define B_SWING     27
#define B_LOCK_REM  28
#define B_POWER     29
#define B_FRESH     31
#define B_SET_TMP   35

byte dataHaier[LEN_B];
byte inCheck      = 0;
long prevPoll     = 0;

// команды Haier
byte qstn[]   = {255,255,10,0,0,0,0,0,1,1,77,1,90};
byte onCmd[]  = {255,255,10,0,0,0,0,0,1,1,77,2,91};
byte offCmd[] = {255,255,10,0,0,0,0,0,1,1,77,3,92};

// >>> OTA
void checkForUpdates();
void otaInstall(const String& url);
bool isNewer(const char* current, const char* latest);
void clearOtaCmdRetain();
// static unsigned long lastCalc = 0;
// Управление RGB-LED
void setColor(uint8_t r, uint8_t g, uint8_t b);
void blinkColor(uint8_t r, uint8_t g, uint8_t b, int times, int delayMs);

// Получить «текущую» версию: сначала из NVS, иначе из компиляторной константы
String getInstalledVersion() {
  versionPrefs.begin("fw", true);
  String v = versionPrefs.getString("version", FW_VERSION);
  versionPrefs.end();
  return v;
}

// Сохранить новую версию после успешного OTA
void saveInstalledVersion(const char* newVer) {
  versionPrefs.begin("fw", false);
  versionPrefs.putString("version", newVer);
  versionPrefs.end();
}


(const byte *buf, size_t sz) {
  byte crc = 0;
  // считаем сумму всех байтов, начиная с индекса 2
  for (int i = 2; i < sz; i++) crc += buf[i];
  return crc;
}

void SendData(const byte *buf, size_t sz) {
  // шлём все байты кроме последнего (crc)
  Serial.write(buf, sz - 1);
  // шлём рассчитанный crc
  Serial.write(getCRC(buf, sz - 1));
}

void InsertData(byte data[], size_t size){
    set_tmp = data[B_SET_TMP]+16;
    cur_tmp = data[B_CUR_TMP];
    Mode = data[B_MODE];
    fan_spd = data[B_FAN_SPD];
    swing = data[B_SWING];
    power = data[B_POWER];
    lock_rem = data[B_LOCK_REM];
    fresh = data[B_FRESH];
  /////////////////////////////////
  if (fresh == 0x00){
      client.publish("myhome/Conditioner/Fresh", "off");
  }
  if (fresh == 0x01){
      client.publish("myhome/Conditioner/Fresh", "on");
  }
  /////////////////////////////////
  if (lock_rem == 0x80){
      client.publish("myhome/Conditioner/Lock_Remote", "true");
  }
  if (lock_rem == 0x00){
      client.publish("myhome/Conditioner/Lock_Remote", "false");
  }
  /////////////////////////////////
  if (power == 0x01 || power == 0x11){
      client.publish("myhome/Conditioner/Power", "on");
  }
  if (power == 0x00 || power == 0x10){
      client.publish("myhome/Conditioner/Power", "off");
  }
  if (power == 0x09){
      client.publish("myhome/Conditioner/Power", "quiet");
  }
  if (power == 0x11 || power == 0x10){
      client.publish("myhome/Conditioner/Compressor", "on");
  } else {
    client.publish("myhome/Conditioner/Compressor", "off");
  }
  /////////////////////////////////
  if (swing == 0x00){
      client.publish("myhome/Conditioner/Swing", "off");
  }
  if (swing == 0x01){
      client.publish("myhome/Conditioner/Swing", "ud");
  }
  if (swing == 0x02){
      client.publish("myhome/Conditioner/Swing", "lr");
  }
  if (swing == 0x03){
      client.publish("myhome/Conditioner/Swing", "all");
  }
  /////////////////////////////////  
  if (fan_spd == 0x00){
      client.publish("myhome/Conditioner/Fan_Speed", "max");
  }
  if (fan_spd == 0x01){
      client.publish("myhome/Conditioner/Fan_Speed", "mid");
  }
  if (fan_spd == 0x02){
      client.publish("myhome/Conditioner/Fan_Speed", "min");
  }
  if (fan_spd == 0x03){
      client.publish("myhome/Conditioner/Fan_Speed", "auto");
  }
  /////////////////////////////////
  char b[5]; 
  String char_set_tmp = String(set_tmp);
  char_set_tmp.toCharArray(b,5);
  client.publish("myhome/Conditioner/Set_Temp", b);
  ////////////////////////////////////
  String char_cur_tmp = String(cur_tmp);
  char_cur_tmp.toCharArray(b,5);
  client.publish("myhome/Conditioner/Current_Temp", b);
  ////////////////////////////////////
  if (Mode == 0x00){
      client.publish("myhome/Conditioner/Mode", "smart");
  }
  if (Mode == 0x01){
      client.publish("myhome/Conditioner/Mode", "cool");
  }
  if (Mode == 0x02){
      client.publish("myhome/Conditioner/Mode", "heat");
  }
  if (Mode == 0x03){
      client.publish("myhome/Conditioner/Mode", "vent");
  }
  if (Mode == 0x04){
      client.publish("myhome/Conditioner/Mode", "dry");
  }
  
  String raw_str;
  char raw[75];
  for (int i=0; i < 37; i++){
     if (data[i] < 10){
       raw_str += "0";
       raw_str += String(data[i], HEX);
     } else {
      raw_str += String(data[i], HEX);
     }    
  }
  raw_str.toUpperCase();
  raw_str.toCharArray(raw,75);
  client.publish("myhome/Conditioner/RAW", raw);
}


/* -------------------------------------------------------------------------- */

void setup() {
  Serial.begin(9600);
  delay(500);
  Serial.println("\n--- ESP32 Dual-Relay Control + HA Discovery ---");

  // Настроим пины
  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(RELAY1_PIN, LOW);
  digitalWrite(RELAY2_PIN, LOW);

  // RGB-LED
  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  setColor(255, 255, 255);  // 1. При запуске — белый

  // Генерация ID и имени
  generateDeviceID();
  Serial.println("Device ID: " + String(device_id));
  Serial.println("Device Name: " + String(device_name));

  // Подготовка топиков
  setupTopics();
  //mqttClient.subscribe(otaCmdTopic.c_str());   // >>> OTA

  // Загрузка MQTT-настроек
  loadMQTTSettings();

  // Настройка WiFiManager
  wm.addParameter(&paramServer);
  wm.addParameter(&paramUser);
  wm.addParameter(&paramPass);
  wm.setTimeout(120);



  // Подключаем Wi-Fi
  if (!connectWiFi(10'000, true)) {
    setColor(255, 0, 0);
    delay(1500);
    openPortalAndRestart();  // сразу переходим в режим AP
  }

  // Настройка MQTT
  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(MQTT_MAX_PACKET_SIZE);

  // Подключаем MQTT
  if (!connectMQTT(10'000)) {
    blinkColor(255, 0, 0, 20, 200);
    openPortalAndRestart();  // сразу переходим в режим AP
  }

  clearOtaCmdRetain();

  /* --- очистили retain ДО подписки --- */
  mqttClient.publish(otaCmdTopic.c_str(), "", true);  // kill INSTALL
  mqttClient.loop();                                  // сразу отправили
  delay(50);                                          // маленькая пауза

  // Подписка на командные и retained state топики
  mqttClient.subscribe(relay1CmdTopic.c_str());
  mqttClient.subscribe(relay2CmdTopic.c_str());
  mqttClient.subscribe(relay1StateTopic.c_str());
  mqttClient.subscribe(relay2StateTopic.c_str());
  mqttClient.subscribe(otaCmdTopic.c_str());
  mqttClient.subscribe("myhome/Conditioner/#");
  Serial.println("Subscribed to:");
  Serial.println(" • " + relay1CmdTopic);
  Serial.println(" • " + relay2CmdTopic);
  Serial.println(" • " + relay1StateTopic + " (retain)");
  Serial.println(" • " + relay2StateTopic + " (retain)");

  // Ждём приёма retained state (200 мс)
  unsigned long t0 = millis();
  while (millis() - t0 < 200) {
    mqttClient.loop();
  }

  // Публикация статуса "online"
  setColor(0, 255, 0);  // зелёный — всё ОК
  publishAvailability();

  // ——— Climate (кондиционер) ———
{
  // подставляем идентификатор и устройство
  String dev = "{\"identifiers\":[\"" + String(device_id) + "\"],"
               "\"name\":\"" + String(device_name) + "\","
               "\"manufacturer\":\"" + manufacturer + "\","
               "\"model\":\"" + DEVICE_MODEL + "\"}";
  // тема конфигурации
  String cfg = "homeassistant/climate/" + String(device_id) + "/config";
  
  // строим JSON-конфиг
  String pl = "{"
    "\"name\":\"" + String(device_name) + "\","
    "\"unique_id\":\"" + String(device_id) + "_climate\","
    "\"mode_state_topic\":\""   + String(relayModeStateTopic)   + "\","
    "\"mode_command_topic\":\"" + String(relayModeCmdTopic)     + "\","
    "\"temperature_state_topic\":\"" + String(tempStateTopic) + "\","
    "\"temperature_command_topic\":\"" + String(tempCmdTopic) + "\","
    "\"current_temperature_topic\":\"" + String(tempStateTopic) + "\","
    "\"min_temp\":16,\"max_temp\":30,\"temp_step\":1,"
    "\"modes\":[\"auto\",\"cool\",\"heat\",\"dry\",\"fan_only\",\"ventilation\"],"
    "\"fan_modes\":[\"min\",\"mid\",\"max\",\"auto\"],"
    "\"swing_modes\":[\"off\",\"ud\",\"lr\",\"all\"],"
    "\"availability_topic\":\""      + availabilityTopic + "\","
    "\"payload_available\":\"online\","
    "\"payload_not_available\":\"offline\","
    "\"device\":" + dev + ","
    "\"retain\":true"
  "}";
  
  mqttClient.publish(cfg.c_str(), pl.c_str(), true);
  Serial.println("Published HA climate discovery → " + cfg);
}

// ——— Power Switch ———
{
  String cfg = "homeassistant/switch/" + String(device_id) + "_power/config";
  String pl = "{"
    "\"name\":\"" + String(device_name) + " Power\","
    "\"unique_id\":\"" + String(device_id) + "_power\","
    "\"command_topic\":\"myhome/Conditioner/Power/set\","
    "\"state_topic\":\"myhome/Conditioner/Power\","
    "\"availability_topic\":\"" + availabilityTopic + "\","
    "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
    "\"device\":{\"identifiers\":[\"" + String(device_id) + "\"]},"
    "\"retain\":true"
  "}";
  mqttClient.publish(cfg.c_str(), pl.c_str(), true);
}


  // ——— Home Assistant Auto-Discovery (relay1) ———
  {
    String dev = "{\"identifiers\":[\"" + String(device_id) + "\"],"
                                                              "\"name\":\""
                 + String(device_name) + "\","
                                         "\"manufacturer\":\""
                 + manufacturer + "\","
                                  "\"model\":\""
                 + DEVICE_MODEL + "\"}";
    String cfg1 = "homeassistant/switch/" + String(device_id) + "_relay1/config";
    String p1 = "{\"name\":\"Contact relay 1\","
                "\"unique_id\":\""
                + String(device_id) + "_relay1\","
                                      "\"command_topic\":\""
                + relay1CmdTopic + "\","
                                   "\"state_topic\":\""
                + relay1StateTopic + "\","
                                     "\"availability_topic\":\""
                + availabilityTopic + "\","
                                      "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
                                      "\"device\":"
                + dev + ","
                        "\"retain\":true}";
    mqttClient.publish(cfg1.c_str(), p1.c_str(), true);
    Serial.println("Discovered Relay1 @ " + cfg1);
  }

// ——— Home Assistant Auto-Discovery (relay2) ———
  {
    String dev = "{\"identifiers\":[\"" + String(device_id) + "\"],"
                                                              "\"name\":\""
                 + String(device_name) + "\","
                                         "\"manufacturer\":\""
                 + manufacturer + "\","
                                  "\"model\":\""
                 + DEVICE_MODEL + "\"}";
    String cfg2 = "homeassistant/switch/" + String(device_id) + "_relay1/config";
    String p2 = "{\"name\":\"Contact relay 1\","
                "\"unique_id\":\""
                + String(device_id) + "_relay1\","
                                      "\"command_topic\":\""
                + relay2CmdTopic + "\","
                                   "\"state_topic\":\""
                + relay2StateTopic + "\","
                                     "\"availability_topic\":\""
                + availabilityTopic + "\","
                                      "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
                                      "\"device\":"
                + dev + ","
                        "\"retain\":true}";
    mqttClient.publish(cfg2.c_str(), p2.c_str(), true);
    Serial.println("Discovered Relay1 @ " + cfg2);
  }

  // >>> OTA entity
  {
    String dev = "{\"identifiers\":[\"" + String(device_id) + "\"],"
                                                              "\"name\":\""
                 + String(device_name) + "\","
                                         "\"manufacturer\":\""
                 + manufacturer + "\","
                                  "\"model\":\""
                 + DEVICE_MODEL + "\"}";

    String pl = "{\"name\":\"Firmware\","
                "\"unique_id\":\""
                + String(device_id) + "_fw\","
                                      "\"state_topic\":\""
                + otaStateTopic + "\","
                                  "\"command_topic\":\""
                + otaCmdTopic + "\","
                                "\"payload_install\":\"INSTALL\","
                                "\"device_class\":\"firmware\","
                                "\"entity_category\":\"diagnostic\","
                                "\"device\":"
                + dev + ",\"retain\":true}";

    mqttClient.publish(otaConfigTopic.c_str(), pl.c_str(), true);
  }

  // >>> MQTT Button "Check FW"
  {
    String dev = "{\"identifiers\":[\"" + String(device_id) + "\"]}";
    String cfg = "homeassistant/button/" + String(device_id) + "_fwcheck/config";
    String pl = "{\"name\":\"" + String(device_name) + " Check FW\","
                                                       "\"unique_id\":\""
                + String(device_id) + "_fwcheck\","
                                      "\"command_topic\":\""
                + otaCmdTopic + "\","
                                "\"payload_press\":\"CHECK\","
                                "\"device\":"
                + dev + ","
                        "\"entity_category\":\"diagnostic\","
                        "\"retain\":true"
                        "}";
    mqttClient.publish(cfg.c_str(), pl.c_str(), true);
  }


  tlsClient.setInsecure();  // >>> OTA (для Let’s Encrypt достаточно)
  checkForUpdates();        // >>> OTA
}


void loop() {
  // Кнопка для портала
  static bool prev = HIGH;
  bool curr = digitalRead(CONFIG_BUTTON_PIN);
  if (prev == HIGH && curr == LOW) {
    Serial.println("Opening config portal...");
    startConfigPortal();
    ESP.restart();
  }
  prev = curr;

  // Reconnect
  if (!mqttClient.connected() && connectMQTT(10000)) {

    clearOtaCmdRetain();

    /* очистка retain перед подпиской */
    mqttClient.publish(otaCmdTopic.c_str(), "", true);
    mqttClient.loop();
    delay(50);

    /* подписка */
    mqttClient.subscribe(relay1CmdTopic.c_str());
    mqttClient.subscribe(relay2CmdTopic.c_str());
    mqttClient.subscribe(relay1StateTopic.c_str());
    mqttClient.subscribe(relay2StateTopic.c_str());
    mqttClient.subscribe(otaCmdTopic.c_str());
    publishAvailability();
  }


  // ——— NEW: обновляем индикацию раз в 1000 мс ———
  static unsigned long lastLed = 0;
  if (millis() - lastLed >= 1000) {
    lastLed = millis();
    updateStatusLED();
  }
  // >>> OTA daily check
  if (millis() - lastOtaCheck > 86'400'000UL) {  // 24 ч
    lastOtaCheck = millis();
    checkForUpdates();
  }

  // ---------- LED section -------------------------------------------
  if (otaInProgress) {                 // дышим зелёным
    if (millis() - lastBreath > 20) {  // скорость дыхания
      lastBreath = millis();
      breathLevel += breathDir;
      if (breathLevel == 0 || breathLevel == 255) breathDir = -breathDir;
      setColor(0, breathLevel, 0);  // только G-канал
    }
  } else {
    static unsigned long lastLed = 0;  // старая индикация
    if (millis() - lastLed >= 1000) {
      lastLed = millis();
      updateStatusLED();
    }
  }

  mqttClient.loop();

  // читаем ответ от Haier
if (Serial.available() >= LEN_B) {
  Serial.readBytes(dataHaier, LEN_B);
  if (dataHaier[36] != inCheck) {
    inCheck = dataHaier[36];
    InsertData(dataHaier, LEN_B);
  }
}

// опрос каждые 5 с
long now = millis();
if (now - prevPoll > 5000) {
  prevPoll = now;
  SendData(qstn, sizeof(qstn));
}
}


/* -------------------------------------------------------------------------- */
/* ==================  ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ  ============================= */
/* -------------------------------------------------------------------------- */

// --- Кнопка + портал ---
bool isConfigPressed() {
  return digitalRead(CONFIG_BUTTON_PIN) == LOW;
}

void openPortalAndRestart() {
  blinkColor(0, 0, 255, 2, 250);  // короткое уведомительное мигание
  setColor(0, 0, 255);            // ⬅️ держим постоянный синий, пока открыт портал
  startConfigPortal();            // WiFiManager (блокирующий)
  setColor(0, 255, 0);            // белый — перезапуск
  delay(800);
  ESP.restart();
}

void clearOtaCmdRetain() {
  mqttClient.publish(otaCmdTopic.c_str(), nullptr, 0, true);  // zero-length
  mqttClient.loop();                                          // выгнать пакет сразу
  delay(50);
}

void generateDeviceID() {
  uint64_t serial = ESP.getEfuseMac();
  char raw[17];
  snprintf(raw, sizeof(raw), "%016llx", serial);
  const char* last6 = &raw[10];
  snprintf(device_id, sizeof(device_id), "%s", last6);
  snprintf(device_name, sizeof(device_name), "%s_%s", DEVICE_NAME_BASE, last6);
}


void setupTopics() {
  baseTopic = "homeassistant";
  availabilityTopic = baseTopic + "/switch/" + device_id + "/availability";
  relay1CmdTopic = baseTopic + "/switch/" + device_id + "/relay1/set";
  relay1StateTopic = baseTopic + "/switch/" + device_id + "/relay1/state";
  relay2CmdTopic = baseTopic + "/switch/" + device_id + "/relay2/set";
  relay2StateTopic = baseTopic + "/switch/" + device_id + "/relay2/state";
    // >>> OTA topics
  otaStateTopic = baseTopic + "/ota/" + String(device_id) + "/state";
  otaCmdTopic = baseTopic + "/ota/" + String(device_id) + "/cmd";
  otaConfigTopic = "homeassistant/update/" + String(device_id) + "/firmware/config";
}

void loadMQTTSettings() {
  prefs.begin("mqtt", false);
  String s = prefs.getString("mqtt_server", "");
  String u = prefs.getString("mqtt_user", "");
  String p = prefs.getString("mqtt_pass", "");
  prefs.end();
  s.toCharArray(mqtt_server, sizeof(mqtt_server));
  u.toCharArray(mqtt_user, sizeof(mqtt_user));
  p.toCharArray(mqtt_pass, sizeof(mqtt_pass));

  if (String(mqtt_server).isEmpty()) strncpy(mqtt_server, DEF_MQTT_SERVER, sizeof(mqtt_server));
  if (String(mqtt_user).isEmpty()) strncpy(mqtt_user, DEF_MQTT_USER, sizeof(mqtt_user));
  if (String(mqtt_pass).isEmpty()) strncpy(mqtt_pass, DEF_MQTT_PASS, sizeof(mqtt_pass));
}
void saveMQTTSettings() {
  prefs.begin("mqtt", false);
  prefs.putString("mqtt_server", mqtt_server);
  prefs.putString("mqtt_user", mqtt_user);
  prefs.putString("mqtt_pass", mqtt_pass);
  prefs.end();
}

/* ---------------------  СЕТЬ  --------------------- */
// Wi-Fi: кнопка опрашивается каждые 100 мс
bool connectWiFi(unsigned long timeout, bool tryDefault) {
  Serial.print("Connecting WiFi… ");

  if (tryDefault) {  // ① сначала дефолт
    WiFi.mode(WIFI_STA);
    WiFi.begin(DEF_WIFI_SSID, DEF_WIFI_PASS);
    uint32_t t0 = millis();
    while (millis() - t0 < 8'000) {  // 8 с достаточно
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("default OK");
        return true;
      }
      delay(250);
    }
    WiFi.disconnect(true);  // сброс пробной сети
    Serial.println("default FAIL");
  }

  // ② затем старая логика — пробуем сохранённые в NVS
  WiFi.begin();
  uint32_t t0 = millis();
  while (millis() - t0 < timeout) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("stored OK");
      return true;
    }
    delay(500);
    Serial.print('.');
  }
  Serial.println(" FAIL");
  return false;
}



bool connectMQTT(unsigned long timeout) {
  Serial.printf("Connecting MQTT '%s'@'%s'…\n", mqtt_user, mqtt_server);
  unsigned long st = millis();
  while (millis() - st < timeout) {
    if (mqttClient.connect(device_id,
                           mqtt_user, mqtt_pass,
                           availabilityTopic.c_str(),
                           1, true, "offline")) {
      Serial.println("MQTT connected");
      mqttClient.publish(availabilityTopic.c_str(), "online", true);
      return true;
    }
    Serial.printf("MQTT rc=%d\n", mqttClient.state());
    delay(20000);
  }
  Serial.println("MQTT timeout");
  return false;
}

/* ------------------  WiFiManager  ----------------- */
void startConfigPortal() {
  String ssid = String(device_name) + "_ConfigAP";
  wm.startConfigPortal(ssid.c_str());
  strncpy(mqtt_server, paramServer.getValue(), sizeof(mqtt_server));
  strncpy(mqtt_user, paramUser.getValue(), sizeof(mqtt_user));
  strncpy(mqtt_pass, paramPass.getValue(), sizeof(mqtt_pass));
  saveMQTTSettings();
  Serial.println("MQTT settings updated, restarting...");
  ESP.restart();
}

/* ------------------  MQTT helpers  ---------------- */
void publishAvailability() {
  mqttClient.publish(availabilityTopic.c_str(), "online", true);
}

// -------------------------------------------------------------------
//  MQTT callback – принимает все входящие сообщения
// -------------------------------------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // превратим payload в строку
  String msg;
  for (unsigned int i = 0; i < len; ++i) msg += (char)payload[i];

  if (String(topic) == otaCmdTopic && msg == "INSTALL" && pendingURL.length() == 0) {
    return;
  }

  Serial.printf("Received %s → %s\n", topic, msg.c_str());

  // ---------- Реле: retained-state  --------------------------------
  if (String(topic) == relay1StateTopic) digitalWrite(RELAY1_PIN, msg == "ON" ? HIGH : LOW);
  else if (String(topic) == relay2StateTopic) digitalWrite(RELAY2_PIN, msg == "ON" ? HIGH : LOW);

  // ---------- Реле: команды ----------------------------------------
  else if (String(topic) == relay1CmdTopic) controlRelay(RELAY1_PIN, msg);
  else if (String(topic) == relay2CmdTopic) controlRelay(RELAY2_PIN, msg);

  // ---------- OTA команды ------------------------------------------
  else if (String(topic) == otaCmdTopic) {
    /* ручная проверка манифеста */
    if (msg == "CHECK") {
      checkForUpdates();
    }

    /* установка прошивки */
    else if (msg == "INSTALL") {
      if (pendingURL.length()) {
        otaInstall(pendingURL);  // всё готово — ставим
      } else {
        Serial.println("[OTA] INSTALL ignored – нет pendingURL");
        // здесь ничего не делаем, значит retain-команда больше не зациклит прошивку
      }
      
    }

    /* сюда можно добавить другие команды, например "CANCEL" */
  }


// 1. Превращаем payload в нуль‑терминированную строку
  payload[length] = '\0';
  String t = String(topic);
  String p = String((char*)payload);

  // 2. Обработка отдельных топиков Haier

  // — Set Temperature —
  if (t == "myhome/Conditioner/Set_Temp") {
    int v = p.toInt() - 16;
    if (v >= 0 && v <= 30) {
      dataHaier[B_SET_TMP] = v;
    }
  }
  // — Mode —
  else if (t == "myhome/Conditioner/Mode") {
    if      (p == "smart") dataHaier[B_MODE] = 0;
    else if (p == "cool")  dataHaier[B_MODE] = 1;
    else if (p == "heat")  dataHaier[B_MODE] = 2;
    else if (p == "vent")  dataHaier[B_MODE] = 3;
    else if (p == "dry")   dataHaier[B_MODE] = 4;
  }
  // — Fan Speed —
  else if (t == "myhome/Conditioner/Fan_Speed") {
    if      (p == "max")  dataHaier[B_FAN_SPD] = 0;
    else if (p == "mid")  dataHaier[B_FAN_SPD] = 1;
    else if (p == "min")  dataHaier[B_FAN_SPD] = 2;
    else if (p == "auto") dataHaier[B_FAN_SPD] = 3;
  }
  // — Swing —
  else if (t == "myhome/Conditioner/Swing") {
    if      (p == "off") dataHaier[B_SWING] = 0;
    else if (p == "ud")  dataHaier[B_SWING] = 1;
    else if (p == "lr")  dataHaier[B_SWING] = 2;
    else if (p == "all") dataHaier[B_SWING] = 3;
  }
  // — Lock Remote —
  else if (t == "myhome/Conditioner/Lock_Remote") {
    dataHaier[B_LOCK_REM] = (p == "true") ? 0x80 : 0x00;
  }
  // — Power —
  else if (t == "myhome/Conditioner/Power") {
    if (p == "on")  { SendData(onCmd,  sizeof(onCmd));  return; }
    if (p == "off") { SendData(offCmd, sizeof(offCmd)); return; }
    if (p == "quiet") dataHaier[B_POWER] = 0x09;
  }
  // — RAW (ввод HEX‑строки и шлём её в кондиционер) —
  else if (t == "myhome/Conditioner/RAW") {
    char buf[75];
    p.toCharArray(buf, 75);
    // распарсим пары HEX‑символов
    for (int i = 0; i < 37; ++i) {
      dataHaier[i] = strtol(buf + i*2, nullptr, 16);
    }
    // шлём напрямую в кондиционер
    Serial.write(dataHaier, 37);
    // и публикуем обратно (если нужно)
    mqttClient.publish("myhome/Conditioner/RAW", buf, true);
    return;
  }

  // 3. В конце: собираем «управленческий» пакет и шлём его
  dataHaier[B_CMD]    = 0x00;
  dataHaier[9]        = 0x01;
  dataHaier[10]       = 0x4D;  // 77
  dataHaier[11]       = 0x5F;  // 95
  SendData(dataHaier, sizeof(dataHaier));

}


void controlRelay(int pin, const String& cmd) {
  String st = (pin == RELAY1_PIN ? relay1StateTopic : relay2StateTopic);
  if (cmd == "ON") {
    digitalWrite(pin, LOW);
    mqttClient.publish(st.c_str(), "ON", true);
  } else if (cmd == "OFF") {
    digitalWrite(pin, HIGH);
    mqttClient.publish(st.c_str(), "OFF", true);
  }
}

/* ------------------  RGB-LED  --------------------- */
void setColor(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(PIN_R, r);
  analogWrite(PIN_G, g);
  analogWrite(PIN_B, b);
}
void blinkColor(uint8_t r, uint8_t g, uint8_t b,
                int times, int delayMs) {
  for (int i = 0; i < times; ++i) {
    setColor(r, g, b);
    delay(delayMs);
    setColor(0, 0, 0);
    delay(delayMs);
  }
}

void updateStatusLED() {
  // 1) Нет Wi-Fi ─ горит КРАСНЫЙ
  if (WiFi.status() != WL_CONNECTED) {
    setColor(255, 0, 0);
    return;
  }

  // 2) Wi-Fi есть, но MQTT обрубился ─ горит ЖЁЛТЫЙ
  if (!mqttClient.connected()) {
    blinkColor(255, 0, 0, 20, 200);
    return;
  }

  // 3) Всё ОК ─ ЗЕЛЁНЫЙ
  setColor(0, 255, 0);
}

/* ================= OTA FUNCTIONS ================= */

bool isNewer(const char* cur, const char* lat) {
  int c1, c2, c3, l1, l2, l3;
  sscanf(cur, "%d.%d.%d", &c1, &c2, &c3);
  sscanf(lat, "%d.%d.%d", &l1, &l2, &l3);
  if (l1 != c1) return l1 > c1;
  if (l2 != c2) return l2 > c2;
  return l3 > c3;
}

void checkForUpdates() {
  Serial.println("\n[OTA] Проверка манифеста…");
  HTTPClient http;
  if (!http.begin(tlsClient, UPDATE_JSON)) {
    Serial.println("[OTA] ❌ http.begin() fail");
    return;
  }

  int code = http.GET();
  Serial.printf("[OTA] HTTP GET = %d\n", code);
  if (code != HTTP_CODE_OK) {
    http.end();
    return;
  }

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, http.getString())) {
    Serial.println("[OTA] ❌ JSON parse error");
    http.end();
    return;
  }

  const char* latest = doc["version"];
  String current = getInstalledVersion();
  const char* url = doc["url"];
  http.end();

  Serial.printf("[OTA] Текущая: %s  /  Новая: %s\n", current.c_str(), latest);

  if (isNewer(current.c_str(), latest)) {
    Serial.println("[OTA] ➜ Доступно обновление!");
    pendingURL = url;
    pendingVersion = String(latest);
  } else {
    Serial.println("[OTA] Уже последняя версия.");
  }

  /* публикуем MQTT-state как раньше … */
  StaticJsonDocument<192> st;
  st["installed_version"] = current;
  st["latest_version"] = latest;
  st["in_progress"] = false;
  char buf[192];
  serializeJson(st, buf);
  mqttClient.publish(otaStateTopic.c_str(), buf, true);
}

void otaInstall(const String& url) {
  Serial.println("\n========== OTA INSTALL ==========");
  Serial.println("[OTA] URL: " + url);

  otaInProgress = true;
  StaticJsonDocument<64> st;
  st["in_progress"] = true;
  char buf[64];
  serializeJson(st, buf);
  mqttClient.publish(otaStateTopic.c_str(), buf, true);

  HTTPClient http;
  if (!http.begin(tlsClient, url)) {
    Serial.println("[OTA] ❌ http.begin() fail");
    otaInProgress = false;
    return;
  }

  int code = http.GET();
  Serial.printf("[OTA] HTTP GET = %d\n", code);
  if (code != HTTP_CODE_OK) {
    http.end();
    otaInProgress = false;
    return;
  }

  int len = http.getSize();
  Serial.printf("[OTA] Content-Length = %d\n", len);

  WiFiClient* stream = http.getStreamPtr();
  if (!Update.begin(len)) {
    Serial.println("[OTA] ❌ Update.begin() fail");
    http.end();
    otaInProgress = false;
    return;
  }

  /* выводим прогресс каждые 5 % */
  Update.onProgress([](size_t done, size_t total) {
    static int last = -5;
    int pct = (done * 100) / total;
    if (pct - last >= 5) {
      last = pct;
      Serial.printf("[OTA] %d %%\n", pct);
    }
  });

  Serial.println("[OTA] Записываем прошивку…");
  size_t written = Update.writeStream(*stream);
  Update.end();
  http.end();
  Serial.printf("[OTA] Записано %u байт\n", (unsigned)written);

  otaInProgress = false;
  st["in_progress"] = false;
  serializeJson(st, buf);
  mqttClient.publish(otaStateTopic.c_str(), buf, true);

  if (written == (size_t)len && Update.isFinished()) {
    Serial.println("[OTA] ✅ Успех! Перезагрузка через 3 с…");
    //saveInstalledVersion(latest);
    if (pendingVersion.length()) {
      saveInstalledVersion(pendingVersion.c_str());
    }
    clearOtaCmdRetain();
    pendingURL = "";
    mqttClient.publish(otaCmdTopic.c_str(), "", true);  // стираем INSTALL
    mqttClient.loop();
    delay(3000);
    ESP.restart();
  } else {
    Serial.println("[OTA] ❌ Ошибка записи! Перезагрузка отменена.");
  }
  Serial.println("=================================\n");
}
