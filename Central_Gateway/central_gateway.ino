#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>

//  LORA 
#define SS 10
#define RST 9
#define DIO0 8
#define SCK 12
#define MISO 13
#define MOSI 11

//  SD 
#define SD_CS   14
#define SD_SCK  18
#define SD_MISO 17
#define SD_MOSI 16
SPIClass sdSPI(HSPI);

//  WEB 
WebServer server(80);
const char* ssid = "Forest Alerts";
const char* pass = "12345678";

//  KEY 
String SECRET_KEY = "NSBM2026";

// DECRYPT 
String decrypt(String payload) {
  String out = "";
  for (int i = 0; i < payload.length(); i++) {
    out += (char)(payload[i] ^ SECRET_KEY[i % SECRET_KEY.length()]);
  }
  return out;
}

//  SD SAVE 
void saveToSD(String msg) {
  File file = SD.open("/log.txt", FILE_APPEND);

  if (file) {
    file.println(msg);
    file.close();
  } else {
    Serial.println("FILE OPEN FAILED");
  }
}



//  READ SD 
String readHistory() {
  File file = SD.open("/log.txt");
  if (!file) return "No Data";

  String data = "";

  while (file.available()) {
    data += "<div class='log'>" + file.readStringUntil('\n') + "</div>";
  }

  file.close();
  return data;
}

//  WEB PAGE 
String page() {
  return
  "<html><head>"
  "<meta name='viewport' content='width=device-width, initial-scale=1'>"
  "<meta http-equiv='refresh' content='2'>"
  "<style>"
  "body{font-family:Arial;background:#0b0f14;color:#e6e6e6;margin:0;padding:12px;}"
  "h2{text-align:center;color:#00ffcc;}"
  ".card{background:#121a22;padding:15px;border-radius:12px;}"
  ".log{background:#1b2632;margin:8px 0;padding:10px;border-radius:8px;font-size:13px;word-break:break-word;}"
  "</style></head><body>"
  "<h2>Alert Dashboard</h2>"
  "<div class='card'>" + readHistory() + "</div>"
  "</body></html>";
}

void handleRoot() {
  server.send(200, "text/html", page());
}

//  SETUP 
void setup() {
  Serial.begin(115200);

  WiFi.softAP(ssid, pass);

  server.on("/", handleRoot);
  server.begin();

  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println("SD FAIL");
    while (1);
  }

  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);

  if (!LoRa.begin(433E6)) {
    Serial.println("LORA FAIL");
    while (1);
  }

  Serial.println("SYSTEM READY");
}

//  LOOP 
void loop() {
  server.handleClient();

  int packetSize = LoRa.parsePacket();
  if (!packetSize) return;

  String raw = "";
  while (LoRa.available()) raw += (char)LoRa.read();

  String msg = decrypt(raw);

  Serial.println(raw);
  Serial.println(msg);

  saveToSD(msg);
}