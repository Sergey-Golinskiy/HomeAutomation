#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

// === Ваши параметры сети и MQTT ===
const char* ssid        = "SmartGrow_AP";
const char* password    = "smartgrow_xoqO7h7o";
const char* mqtt_server = "192.168.1.100";

WiFiClient   espClient;
PubSubClient client(espClient);

#define ID_CONNECT "Haier_Controller"
#define LED_PIN    D4       // встроенный светодиод на Wemos

#define LEN_B      37
#define B_CUR_TMP   13
#define B_CMD       17
#define B_MODE      23
#define B_FAN_SPD   25
#define B_SWING     27
#define B_LOCK_REM  28
#define B_POWER     29
#define B_FRESH     31
#define B_SET_TMP   35

byte dataArr[LEN_B];
byte inCheck = 0;
unsigned long prev = 0;

// Команды для Haier (опрос/вкл/выкл)
byte qstn[] = {255,255,10,0,0,0,0,0,1,1,77,1,90};
byte on[]   = {255,255,10,0,0,0,0,0,1,1,77,2,91};
byte off[]  = {255,255,10,0,0,0,0,0,1,1,77,3,92};

// ======== Функции ========
byte getCRC(const byte *buf, size_t sz){
  byte crc=0;
  for(int i=2;i<sz;i++) crc+=buf[i];
  return crc;
}

void SendData(const byte *buf, size_t sz){
  Serial.write(buf, sz-1);
  Serial.write(getCRC(buf, sz-1));
}

void InsertData(const byte *buf){
  int set_t  = buf[B_SET_TMP] + 16;
  int cur_t  = buf[B_CUR_TMP];
  byte mode  = buf[B_MODE];
  byte fan   = buf[B_FAN_SPD];
  byte swing = buf[B_SWING];
  byte lockR = buf[B_LOCK_REM];
  byte power = buf[B_POWER];
  byte fresh = buf[B_FRESH];

  client.publish("haier/Fresh",      fresh ? "on":"off");
  client.publish("haier/Lock_Remote",lockR ? "true":"false");
  client.publish("haier/Power",      (power&1) ? "on":"off");
  // … публикация Compressor, Swing, Fan_Speed, Set_Temp, Current_Temp, Mode

  // RAW HEX-массив
  char raw[LEN_B*2+1];
  for(int i=0;i<LEN_B;i++) sprintf(raw+i*2, "%02X", buf[i]);
  client.publish("haier/RAW", raw);
}

void callback(char* topic, byte* payload, unsigned int length){
  String t=String(topic), p=String((char*)payload).substring(0,length);

  if(t.endsWith("Set_Temp")){
    int v=p.toInt()-16;
    if(v>=0&&v<=30) dataArr[B_SET_TMP]=v;
  }
  if(t.endsWith("Mode")){
    if(p=="cool")  dataArr[B_MODE]=1;
    if(p=="heat")  dataArr[B_MODE]=2;
    if(p=="dry")   dataArr[B_MODE]=4;
    if(p=="smart") dataArr[B_MODE]=0;
    if(p=="vent")  dataArr[B_MODE]=3;
  }
  if(t.endsWith("Power")){
    if(p=="on"){  SendData(on, sizeof(on));  return; }
    if(p=="off"){ SendData(off,sizeof(off)); return; }
  }
  // … аналогично Fan_Speed, Swing, Lock_Remote

  dataArr[B_CMD]=0; dataArr[9]=1; dataArr[10]=77; dataArr[11]=95;
  SendData(dataArr, sizeof(dataArr));
}

void setup_wifi(){
  WiFi.begin(ssid,password);
  while(WiFi.status()!=WL_CONNECTED){
    digitalWrite(LED_PIN,!digitalRead(LED_PIN));
    delay(500);
  }
  digitalWrite(LED_PIN,HIGH);
}

void reconnect(){
  while(!client.connected()){
    if(client.connect(ID_CONNECT)){
      client.subscribe("haier/#");
      client.publish("haier/connection","true");
    } else delay(5000);
  }
}

void setup(){
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(9600);
  setup_wifi();

  client.setServer(mqtt_server,1883);
  client.setCallback(callback);

  ArduinoOTA.setHostname("HaierD1mini");
  ArduinoOTA.begin();
}

void loop(){
  ArduinoOTA.handle();

  if(Serial.available()>=LEN_B){
    Serial.readBytes(dataArr, LEN_B);
    if(dataArr[36]!=inCheck){
      inCheck=dataArr[36];
      InsertData(dataArr);
    }
  }

  if(!client.connected()) reconnect();
  client.loop();

  if(millis()-prev>5000){
    prev=millis();
    SendData(qstn,sizeof(qstn));
  }
}
