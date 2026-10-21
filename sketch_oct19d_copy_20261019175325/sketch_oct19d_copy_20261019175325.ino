#include <SPI.h>
#include <MFRC522.h>
#include <WiFiS3.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <PubSubClient.h> // ไลบรารีสำหรับ MQTT

// ==========================================
// 🔴 1. ตั้งค่า WiFi และ MQTT
// ==========================================
const char* ssid = "bambam";
const char* password = "12345678";

// ตั้งค่า MQTT Broker (ใช้ของฟรีจาก HiveMQ)
const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic = "it_project/attendance/room1";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200, 60000);

// ==========================================
// 🔴 2. ตั้งค่าเซนเซอร์
// ==========================================
#define SS_PIN  10
#define RST_PIN 9
MFRC522 rfid(SS_PIN, RST_PIN);

#define THERMISTOR_PIN A0
#define LED_GREEN 7
#define LED_RED   6

const float SERIES_RESISTOR = 10000.0;
const float NOMINAL_RESISTANCE = 10000.0;
const float NOMINAL_TEMPERATURE = 25.0;
const float B_COEFFICIENT = 3950.0;

void setup_wifi() {
  delay(10);
  Serial.print("กำลังเชื่อมต่อ WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi เชื่อมต่อสำเร็จ!");
}

void reconnect_mqtt() {
  // วนลูปจนกว่าจะต่อ MQTT สำเร็จ
  while (!mqttClient.connected()) {
    Serial.print("พยายามเชื่อมต่อ MQTT Broker...");
    // สร้างชื่อ Client ID แบบสุ่มป้องกันการชนกัน
    String clientId = "ArduinoR4Client-";
    clientId += String(random(0xffff), HEX);
    
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("เชื่อมต่อ MQTT สำเร็จ!");
    } else {
      Serial.print("ล้มเหลว, รหัส error=");
      Serial.print(mqttClient.state());
      Serial.println(" รอ 5 วินาทีแล้วลองใหม่");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, LOW);

  SPI.begin();
  rfid.PCD_Init();
  
  setup_wifi();
  
  mqttClient.setServer(mqtt_server, 1883); // พอร์ต 1883 สำหรับบอร์ด Arduino
  
  timeClient.begin();
  
  Serial.println("=============================================");
  Serial.println(" ระบบพร้อม! แตะบัตร -> บีบเซนเซอร์ค้างไว้ 5 วิ ");
  Serial.println("=============================================");
}

float readTemperature() {
  int analogValue = analogRead(THERMISTOR_PIN);
  if (analogValue == 0) return 0.0; 
  
  float resistance = SERIES_RESISTOR / (1023.0 / analogValue - 1.0);
  float steinhart = resistance / NOMINAL_RESISTANCE;
  steinhart = log(steinhart) / B_COEFFICIENT;
  steinhart += 1.0 / (NOMINAL_TEMPERATURE + 273.15);
  steinhart = 1.0 / steinhart;
  return steinhart - 273.15;
}

void loop() {
  // เช็กว่า WiFi และ MQTT ยังเชื่อมต่ออยู่ไหม ถ้าหลุดให้ต่อใหม่
  if (!mqttClient.connected()) {
    reconnect_mqtt();
  }
  mqttClient.loop();
  timeClient.update();

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  String cardUID = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) cardUID += "0";
    cardUID += String(rfid.uid.uidByte[i], HEX);
  }
  cardUID.toUpperCase();

  Serial.println("แตะบัตรแล้ว! กำลังวัดอุณหภูมิ กรุณาบีบเซนเซอร์...");

  float maxBodyTemp = 0.0;
  for(int i = 0; i < 50; i++) { 
    float currentTemp = readTemperature();
    if (currentTemp > maxBodyTemp) {
      maxBodyTemp = currentTemp; 
    }
    digitalWrite(LED_GREEN, HIGH);
    delay(50);
    digitalWrite(LED_GREEN, LOW);
    delay(50);
  }

  float finalDisplayTemp = maxBodyTemp + 4.0; 
  String currentTime = timeClient.getFormattedTime(); 

  // ==========================================
  // 🔴 3. จัดรูปแบบ JSON และ Publish ขึ้น MQTT
  // ==========================================
  // จะได้หน้าตาแบบนี้: {"uid":"A1B2C3D4", "temp":36.5, "time":"08:30:00"}
  String jsonPayload = "{\"uid\":\"" + cardUID + "\",\"temp\":" + String(finalDisplayTemp, 1) + ",\"time\":\"" + currentTime + "\"}";
  
  // ส่งขึ้นเน็ต!
  mqttClient.publish(mqtt_topic, jsonPayload.c_str());
  
  // ปริ้นต์ลงจอคอมให้เราดูด้วยเพื่อความชัวร์
  Serial.print("ส่งข้อมูลสำเร็จ: ");
  Serial.println(jsonPayload);

  digitalWrite(LED_GREEN, HIGH);
  delay(1000);
  digitalWrite(LED_GREEN, LOW);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  delay(1000); 
}