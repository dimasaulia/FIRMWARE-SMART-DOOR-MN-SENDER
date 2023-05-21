#include <Arduino.h>
//
#include "SPIFFS.h"
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Key.h>
#include <Keypad.h>
#include <Keypad_I2C.h>
#include <MFRC522.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>
#include <namedMesh.h>
#include <secret.h>

#define MESH_SSID "smartdoornetwork"
#define MESH_PASSWORD "t4np454nd1"
#define LED 2
#define MESH_PORT 5555
#define RTO_LIMIT 8000          // 8s
#define DOOR_OPEN_DURATION 5000 // 5s
#define I2C_KEYPAD_ADDR 0x38
#define TOUCH 33
#define RELAY 27
#define LED_GATEWAY 32
// #define LED_TX 16
// #define LED_RX 17

String DATA_SSID; // Variables to save values from HTML form
String DATA_PASSWORD;
String DATA_GATEWAY;
String DEVICE_MODE = "AUTH"; // or ADMIN
String DATA_NODE;
String cardIdContainer;
String pinContainer;
boolean isWaitingForAuthResponse = false;
boolean isWaitingForConnectionStartupResponse = false;
boolean isWaitingForConnectionPingResponse = false;
boolean isConnectionReady = false;
boolean isResponseDestinationCorrect = false;
boolean isGatewayAvailable = false;
boolean isDoorOpen = false;
boolean isCardExist = false;
boolean isDeviceAllowToSendAuth = false;
boolean APStatus = false;
boolean changeMode = false;
unsigned long connectionStartupCheckTime = 0;
unsigned long connectionStartupRTOChecker = 0;
unsigned long connectionPingCheckTime = 0;
unsigned long connectionPingRTOChecker = 0;
unsigned long authCheckTime = 0;
unsigned long rfidScanTime = 0;
unsigned long authRTOChecker = 0;
unsigned long doorTimestamp = 0;
unsigned long bootTimestamp = 0;
const short RFID_RST = 4;
const short RFID_SS = 15;
const char *PARAM_INPUT_1 = "ssid"; // Search for parameter in HTTP POST request
const char *PARAM_INPUT_2 = "password";
const char *PARAM_INPUT_3 = "node";
const char *PARAM_INPUT_4 = "gateway";
const int TOUCH_RESET_PIN = 4;
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
    // Flat Keypad
    {'D', 'C', 'B', 'A'},
    {'#', '9', '6', '3'},
    {'0', '8', '5', '2'},
    {'*', '7', '4', '1'},
};

// File paths to save input values permanentlys
const char *nodePath = "/node.txt";
const char *ssidPath = "/ssid.txt";
const char *passwordPath = "/password.txt";
const char *gatewayPath = "/gateway.txt";

// Class Instance
Scheduler userScheduler;
namedMesh mesh;
AsyncWebServer server(80); // Create AsyncWebServer object on port 80
IPAddress localIP;         // Set IP address
IPAddress localGateway;
IPAddress subnet(255, 255, 0, 0);
MFRC522 rfid(RFID_SS, RFID_RST);
byte rowPins[ROWS] = {0, 1, 2, 3};
byte colPins[COLS] = {4, 5, 6, 7};
Keypad_I2C keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS,
                  I2C_KEYPAD_ADDR, PCF8574);

// Initialize SPIFFS
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("[x]: An error has occurred while mounting SPIFFS");
  }
  Serial.println("[x]: SPIFFS mounted successfully");
}

// Read File from SPIFFS
String readFile(fs::FS &fs, const char *path) {
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("[x]: failed to open file for reading");
    return String();
  }

  String fileContent;
  while (file.available()) {
    fileContent = file.readStringUntil('\n');
    break;
  }
  return fileContent;
}

// Write file to SPIFFS
void writeFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("[x]: Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("[x]: failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("[x]: file written");
  } else {
    Serial.println("[x]: write failed");
  }
}

// Initialize Mesh
bool meshStatus() {
  if (DATA_SSID == "" || DATA_PASSWORD == "" || DATA_GATEWAY == "" ||
      DATA_NODE == "") {
    Serial.println("[x]: Undefined SSID, Password, Gateway, Node Value.");
    return false;
  }
  return true;
}

// Reset Mesh Network
void meshReset() {
  Serial.println("[x]: Attempting To Reset ESP Mesh Network");
  String empty = "";
  writeFile(SPIFFS, ssidPath, empty.c_str());
  writeFile(SPIFFS, passwordPath, empty.c_str());
  writeFile(SPIFFS, nodePath, empty.c_str());
  writeFile(SPIFFS, gatewayPath, empty.c_str());
  delay(3000);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  bootTimestamp = millis();

  // Start Instance
  initSPIFFS();

  // Pin Control
  pinMode(LED, OUTPUT);
  pinMode(RELAY, OUTPUT);
  pinMode(LED_GATEWAY, OUTPUT);
  digitalWrite(LED, LOW);
  digitalWrite(RELAY, LOW);
  digitalWrite(LED_GATEWAY, LOW);

  // read variable
  DATA_SSID = readFile(SPIFFS, ssidPath);
  DATA_PASSWORD = readFile(SPIFFS, passwordPath);
  DATA_NODE = readFile(SPIFFS, nodePath);
  DATA_GATEWAY = readFile(SPIFFS, gatewayPath);
  Serial.print("[x]: SSID: ");
  Serial.println(DATA_SSID);
  Serial.print("[x]: Password: ");
  Serial.println(DATA_PASSWORD);
  Serial.print("[x]: Node: ");
  Serial.println(DATA_NODE);
  Serial.print("[x]: Gateway: ");
  Serial.println(DATA_GATEWAY);

  if (meshStatus() == false) {
    // Connect to ESP Mesh network with SSID and password
    APStatus = true;
    Serial.println("[x]: Setting AP (Access Point)");
    // NULL sets an open Access Point
    WiFi.softAP("ESP-MESH-MANAGER", NULL);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("[x]: AP IP address: ");
    Serial.println(IP);

    // Web Server Root URL
    server.serveStatic("/", SPIFFS, "/");
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(SPIFFS, "/index.html", "text/html");
    });

    // Handling POST Request
    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
      int params = request->params();
      for (int i = 0; i < params; i++) {
        AsyncWebParameter *p = request->getParam(i);
        if (p->isPost()) {
          // HTTP POST ssid value
          if (p->name() == PARAM_INPUT_1) {
            DATA_SSID = p->value().c_str();
            Serial.print("[x]: SSID set to: ");
            Serial.println(DATA_SSID);
            // Write file to save value
            writeFile(SPIFFS, ssidPath, DATA_SSID.c_str());
          }
          // HTTP POST password value
          if (p->name() == PARAM_INPUT_2) {
            DATA_PASSWORD = p->value().c_str();
            Serial.print("[x]: Password set to: ");
            Serial.println(DATA_PASSWORD);
            // Write file to save value
            writeFile(SPIFFS, passwordPath, DATA_PASSWORD.c_str());
          }
          // HTTP POST Node value
          if (p->name() == PARAM_INPUT_3) {
            DATA_NODE = p->value().c_str();
            Serial.print("[x]: Node value set to: ");
            Serial.println(DATA_NODE);
            // Write file to save value
            writeFile(SPIFFS, nodePath, DATA_NODE.c_str());
          }
          // HTTP POST gateway value
          if (p->name() == PARAM_INPUT_4) {
            DATA_GATEWAY = p->value().c_str();
            Serial.print("[x]: Gateway set to: ");
            Serial.println(DATA_GATEWAY);
            // Write file to save value
            writeFile(SPIFFS, gatewayPath, DATA_GATEWAY.c_str());
          }
        }
      }
      request->send(200, "text/plain",
                    "Done. ESP will restart, connect to ESP Mesh Network  ");
      APStatus = false;
      delay(3000);
      ESP.restart();
    });

    server.begin();
  }

  if (meshStatus() == true && APStatus == false) {
    Serial.println("[x]: Trying to connect to esp mesh");
    mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);
    mesh.init(DATA_SSID, DATA_PASSWORD, &userScheduler, MESH_PORT);
    mesh.setName(DATA_NODE);

    mesh.onReceive([](String &from, String &msg) {
      digitalWrite(LED, HIGH);
      Serial.printf("[i]: Get Response From Gateway: %s. %s\n", from.c_str(),
                    msg.c_str());

      // Pastikan Data Yang Diterima Memang Ditujukan Untuk Node Ini, Ubah Data
      // menjadi JSON Terlebih dahulu
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, msg.c_str());
      if (error) {
        Serial.println("[e]: Failed to deserializeJson");
        Serial.println(error.f_str());
        return;
      }

      String destination =
          doc["destination"]; // Destinasi akhir data yang diterima,dan akan
                              // menjadi sumber data (source) ketika response
                              // balik diberikan
      String source =
          doc["source"]; // Sumber pengirim data, dan akan menjadi destinasi
                         // akhir ketika response balik diberikan
      String type = doc["type"];

      // memastikan data yang ditujukan sesuai,
      // *opsional untuk melakukan pengecekan dari mana sumber data yang
      // diterima
      if (destination == DATA_NODE) {
        isResponseDestinationCorrect = true;
      }

      // INFO: Jika tipe response yang diterima adalah "auth"
      // Melihat response apakah pintu bisa dibuka
      if (isWaitingForAuthResponse && isResponseDestinationCorrect &&
          doc["success"] == true && type == "auth") {
        unsigned long arrivalTime = millis();
        unsigned long waitingTime = arrivalTime - authRTOChecker;
        Serial.println("[i]: Auth request start on " + String(authRTOChecker) +
                       " receive response on " + String(arrivalTime) +
                       " final response time " + String(waitingTime));
        doorTimestamp = millis();
        Serial.println("[i]: Sukses Membuka Pintu");
        isWaitingForAuthResponse = false;     // reset value
        isResponseDestinationCorrect = false; // reset value
        isDoorOpen = true;
      }

      if (isWaitingForAuthResponse && isResponseDestinationCorrect &&
          doc["success"] == false && type == "auth") {
        isWaitingForAuthResponse = false;     // reset value
        isResponseDestinationCorrect = false; // reset value
        isDoorOpen = false;
        Serial.println("[x]: Failed To Open Room");
      }

      // INFO: Jika response yang diterima adalah "connectionstartup"
      if (isWaitingForConnectionStartupResponse &&
          isResponseDestinationCorrect && doc["success"] == true &&
          type == "connectionstartup") {
        unsigned long waitingTime = millis() - connectionStartupRTOChecker;
        Serial.println("[x]: Connection Ready in " +
                       String(millis() - bootTimestamp) + " ms");
        Serial.println("[x]: Connestion Latency " + String(waitingTime) +
                       " ms");
        isWaitingForConnectionStartupResponse = false; // reset value
        isResponseDestinationCorrect = false;          // reset value
        isConnectionReady = true;  // allow device to operate
        isGatewayAvailable = true; // alllow user to tap their card
        digitalWrite(LED_GATEWAY, HIGH);
      }

      // INFO: Jika response yang diterima adalah "connectionping"
      if (isWaitingForConnectionPingResponse && isResponseDestinationCorrect &&
          doc["success"] == true && doc["type"] == "connectionping") {
        unsigned long arrivalTime = millis();
        unsigned long waitingTime = arrivalTime - connectionPingRTOChecker;
        Serial.println("[i]: Connection Ping start on " +
                       String(connectionPingRTOChecker) + " receive reply on " +
                       String(arrivalTime) + " final response time " +
                       String(waitingTime));
        isWaitingForConnectionPingResponse = false; // reset value
        isResponseDestinationCorrect = false;       // reset value
        digitalWrite(LED_GATEWAY, HIGH);
        Serial.println("[x]: Gateway Still Available");
      }
      digitalWrite(LED, LOW);
    });

    mesh.onChangedConnections(
        []() { Serial.printf("[x]: Changed Connection\n"); });
  }

  // When First Start Up try to connect to gateway
  while (isConnectionReady == false && APStatus == false) {
    mesh.update();
    // MENGIRIM PESAN SETIAP 5 DETIK
    uint64_t now = millis();
    if (now - connectionStartupCheckTime > 5000) {
      Serial.println("[x]: Sending Connection Startup");
      connectionStartupCheckTime = millis();
      String msg = "{\"type\":\"connectionstartup\", \"source\":\"" +
                   DATA_NODE + "\", \"destination\" : \"" + DATA_GATEWAY +
                   +"\"}";
      connectionStartupRTOChecker = millis();
      mesh.sendSingle(DATA_GATEWAY, msg);
      isWaitingForConnectionStartupResponse = true;
    }
  }

  if (isConnectionReady && APStatus == false) {
    Serial.println("[x]: Waking Up I2C & Keypad");
    Wire.begin();
    keypad.begin(makeKeymap(keys));
    Serial.println("[x]: Waking Up RFID Reader");
    delay(1000);
    SPI.begin();
    rfid.PCD_Init();
    Serial.println("[x]: Device Ready");
  }
}

long id = 0;
int reading = 100;
void loop() {
  // INFO: RFID
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    cardIdContainer = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      cardIdContainer.concat(String(rfid.uid.uidByte[i] < 0x10 ? " 0" : ""));
      cardIdContainer.concat(String(rfid.uid.uidByte[i], HEX));
    }
    // Serial.printf("[x]: CARD ID %s\n", cardIdContainer);

    //  Jika Waktu Menempel kartu masih kurang dari dua detik dari kartu
    //  terakhir yang di tempelkan maka jangan ijinkan mengirim data

    if (rfidScanTime != 0 && millis() - rfidScanTime < 2000) {
      // Serial.println("[x]: Device Cant Send Request");
      isDeviceAllowToSendAuth = false;
    } else {
      isCardExist = true;
      isDeviceAllowToSendAuth = true;
      rfidScanTime = millis();
      // Serial.println("[x]: Device Can Send Request");
    }
  }

  // INFO: CONNECTION
  if (isConnectionReady) {
    mesh.update();

    // INFO: KEYPAD
    char key = keypad.getKey(); // get keypad value

    // Storing keypad value to variable
    if (key) {
      if (key != 'A' && key != 'B' && key != 'C' && key != 'D' && key != '*' &&
          key != '#') {
        if (pinContainer.length() < 6) {
          pinContainer += key;
        }
        Serial.printf("Current Pin: %s\n", pinContainer);
      }
    }

    // Clearing Pin Container
    if (key == 'C') {
      pinContainer = "";
      isCardExist = false;
      cardIdContainer = "";
      Serial.println("[x]: Clearing All Stored Data");
    }

    // Changing Mode
    if (key == '#') {
      pinContainer = "";
      cardIdContainer = "";
      isCardExist = false;
      if (changeMode == false) {
        changeMode = true;
        Serial.println("[x]: Change Mode True");
      } else {
        changeMode = false;
        Serial.println("[x]: Change Mode False");
      }
    }

    // Authenticate To Change Device Mode
    if (key == 'D' && changeMode) {
      if (pinContainer == DEVICE_SEC_PIN) {
        if (DEVICE_MODE == "AUTH") {
          DEVICE_MODE = "ADMIN";
        } else {
          DEVICE_MODE = "AUTH";
        }

        Serial.printf("[x]: Success Changing Mode to %s\n", DEVICE_MODE);
      }

      if (pinContainer != DEVICE_SEC_PIN) {
        Serial.println("[x]: Failed to success Changing Mode");
      }

      isCardExist = false;
      cardIdContainer = "";
      pinContainer = "";
    }

    // Reset Device Setting
    if (key == 'B' && DEVICE_MODE == "ADMIN") {
      Serial.println("[x]: Reset Device Setting");
      meshReset();
    }

    // If Reset  failed
    if (key == 'B' && DEVICE_MODE != "ADMIN") {
      Serial.println("[x]: Can't reset device setting, operation only can be "
                     "performed by ADMIN");
    }

    // MENGIRIM PESAN SETIAP 10 DETIK
    uint64_t now = millis();

    if (isCardExist && isDeviceAllowToSendAuth && DEVICE_MODE == "AUTH" &&
        changeMode == false) {
      String msg = "{\"msgid\" : \"" + String(id) +
                   "\",\"type\":\"auth\",\"source\" : \"" + DATA_NODE +
                   "\",\"destination\" : \"" + DATA_GATEWAY +
                   "\",\"card\": "
                   "{\"id\" : \"" +
                   cardIdContainer + "\",\"pin\" : \"" + pinContainer + "\"}}";
      authCheckTime = millis();
      authRTOChecker = millis();
      Serial.println("[i]: Sending Request To Gateway");
      Serial.printf("[x]: CARD ID %s\n", cardIdContainer);
      Serial.printf("[x]: CARD PIN %s\n", pinContainer);
      mesh.sendSingle(DATA_GATEWAY, msg);
      isWaitingForAuthResponse = true;
      Serial.println("[x]: Clearing All Stored Data");
      isCardExist = false;
      cardIdContainer = "";
      pinContainer = "";
      id++;
    }

    if (isWaitingForAuthResponse && millis() - authRTOChecker > RTO_LIMIT) {
      Serial.println("[i]: AUTH RTO");
      isWaitingForAuthResponse = false;
    }

    // if (isWaitingForAuthResponse) {
    //   // Lakukan Sesuatu Ketika Sedang Menunggu Response
    //   Serial.println("[x]: Waiting...");
    // }

    // Jika pintu bisa dibuka dan waktu sekarang dikurang waktu pertama perintah
    // untuk membuka pintu
    if (isDoorOpen && millis() - doorTimestamp < DOOR_OPEN_DURATION) {
      // Lakukan Sesuatu Ketika Pintu Bisa Dibuka
      // Relay Menyala Untuk Membuka Pintu
      digitalWrite(RELAY, HIGH);
    }

    // Jika Sudah melebihi batas waktu durasi membuka pintu maka matikan relay
    if (isDoorOpen && millis() - doorTimestamp > DOOR_OPEN_DURATION) {
      // Lakukan Sesuatu Ketika Pintu Bisa Ditutup
      // Relay Mati Pintu, Kembali terkunci
      isDoorOpen = false;
      digitalWrite(RELAY, LOW);
    }

    // INFO: Ketersediaan Gateway
    // Lakukan ping setiap 20 detik untuk melihat ketersediaan gateway
    if (now - connectionPingCheckTime > 20000) {
      connectionPingCheckTime = millis();
      Serial.println("[x]: Sending Connection Ping");
      String msg = "{\"type\":\"connectionping\", \"source\":\"" + DATA_NODE +
                   "\", \"destination\" : \"" + DATA_GATEWAY + +"\"}";
      mesh.sendSingle(DATA_GATEWAY, msg);
      connectionPingRTOChecker = millis();
      isWaitingForConnectionPingResponse = true;
    }

    // Jika Ping tidak berbalas
    if (isWaitingForConnectionPingResponse &&
        millis() - connectionPingRTOChecker > RTO_LIMIT) {
      Serial.println("[x]: PING RTO");
      isWaitingForConnectionPingResponse = false;
      digitalWrite(LED_GATEWAY, LOW);
    }
  }
}