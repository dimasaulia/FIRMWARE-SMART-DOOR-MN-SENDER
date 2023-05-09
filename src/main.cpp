#include <Arduino.h>
//
#include "SPIFFS.h"
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <namedMesh.h>

#define MESH_SSID "smartdoornetwork"
#define MESH_PASSWORD "t4np454nd1"
#define LED 2
#define MESH_PORT 5555
// #define RTO_LIMIT 10000         // 10s
#define RTO_LIMIT 8000          // 8s
#define DOOR_OPEN_DURATION 5000 // 5s

String DATA_SSID; // Variables to save values from HTML form
String DATA_PASSWORD;
String DATA_GATEWAY;
String DATA_NODE;
boolean isWaitingForAuthResponse = false;
boolean isWaitingForConnectionStartupResponse = false;
boolean isWaitingForConnectionPingResponse = false;
boolean isConnectionReady = false;
boolean isResponseDestinationCorrect = false;
boolean isGatewayAvailable = false;
boolean isDoorOpen = false;
boolean APStatus = false;
unsigned long connectionStartupCheckTime = 0;
unsigned long connectionStartupRTOChecker = 0;
unsigned long connectionPingCheckTime = 0;
unsigned long connectionPingRTOChecker = 0;
unsigned long authCheckTime = 0;
unsigned long authRTOChecker = 0;
unsigned long doorTimestamp = 0;
const char *PARAM_INPUT_1 = "ssid"; // Search for parameter in HTTP POST request
const char *PARAM_INPUT_2 = "password";
const char *PARAM_INPUT_3 = "node";
const char *PARAM_INPUT_4 = "gateway";
const int TOUCH_RESET_PIN = 4;

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

// Initialize SPIFFS
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("[e]: An error has occurred while mounting SPIFFS");
  }
  Serial.println("[x]: SPIFFS mounted successfully");
}

// Read File from SPIFFS
String readFile(fs::FS &fs, const char *path) {
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
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
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
}

// Initialize Mesh
bool meshStatus() {
  if (DATA_SSID == "" || DATA_PASSWORD == "" || DATA_GATEWAY == "" ||
      DATA_NODE == "") {
    Serial.println("[e]: Undefined SSID, Password, Gateway, Node Value.");
    return false;
  }
  return true;
}

// Reset Mesh Network
void meshReset() {
  Serial.println("Attempting To Reset ESP Mesh Network");
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
  initSPIFFS();

  // Pin Controll
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  // read variable
  DATA_SSID = readFile(SPIFFS, ssidPath);
  DATA_PASSWORD = readFile(SPIFFS, passwordPath);
  DATA_NODE = readFile(SPIFFS, nodePath);
  DATA_GATEWAY = readFile(SPIFFS, gatewayPath);
  Serial.print("SSID: ");
  Serial.println(DATA_SSID);
  Serial.print("Password: ");
  Serial.println(DATA_PASSWORD);
  Serial.print("Node: ");
  Serial.println(DATA_NODE);
  Serial.print("Gateway: ");
  Serial.println(DATA_GATEWAY);

  if (meshStatus() == false) {
    // if (true) {
    // Connect to ESP Mesh network with SSID and password
    APStatus = true;
    Serial.println("Setting AP (Access Point)");
    // NULL sets an open Access Point
    WiFi.softAP("ESP-MESH-MANAGER", NULL);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    // Web Server Root URL
    // server.serveStatic("/", SPIFFS, "/");
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
            Serial.print("SSID set to: ");
            Serial.println(DATA_SSID);
            // Write file to save value
            writeFile(SPIFFS, ssidPath, DATA_SSID.c_str());
          }
          // HTTP POST password value
          if (p->name() == PARAM_INPUT_2) {
            DATA_PASSWORD = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(DATA_PASSWORD);
            // Write file to save value
            writeFile(SPIFFS, passwordPath, DATA_PASSWORD.c_str());
          }
          // HTTP POST Node value
          if (p->name() == PARAM_INPUT_3) {
            DATA_NODE = p->value().c_str();
            Serial.print("Node value set to: ");
            Serial.println(DATA_NODE);
            // Write file to save value
            writeFile(SPIFFS, nodePath, DATA_NODE.c_str());
          }
          // HTTP POST gateway value
          if (p->name() == PARAM_INPUT_4) {
            DATA_GATEWAY = p->value().c_str();
            Serial.print("Gateway set to: ");
            Serial.println(DATA_GATEWAY);
            // Write file to save value
            writeFile(SPIFFS, gatewayPath, DATA_GATEWAY.c_str());
          }
          // Serial.printf("POST[%s]: %s\n", p->name().c_str(),
          // p->value().c_str());
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
    Serial.println("Trying to connect to esp mesh");
    mesh.setDebugMsgTypes(ERROR | STARTUP);
    mesh.init(DATA_SSID, DATA_PASSWORD, &userScheduler, MESH_PORT);
    mesh.setName(DATA_NODE);

    mesh.onReceive([](String &from, String &msg) {
      digitalWrite(LED, HIGH);
      // Serial.printf("[i]: Get Response From Gateway: %s. %s\n", from.c_str(),
      //               msg.c_str());

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

      // INFO: Jika response yang diterima adalah "connectionstartup"
      if (isWaitingForConnectionStartupResponse &&
          isResponseDestinationCorrect && doc["success"] == true &&
          type == "connectionstartup") {
        unsigned long waitingTime = millis() - connectionStartupRTOChecker;
        Serial.println("[x]: Connection Ready in " + String(waitingTime) +
                       " ms");
        Serial.println("[x]: Device Ready");
        isWaitingForConnectionStartupResponse = false; // reset value
        isResponseDestinationCorrect = false;          // reset value
        isConnectionReady = true;  // allow device to operate
        isGatewayAvailable = true; // alllow user to tap their card
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
        Serial.println("[x]: Gateway Still Available");
        // isConnectionReady = true;  // allow device to operate
        // isGatewayAvailable = true; // alllow user to tap their card
      }
      digitalWrite(LED, LOW);
    });

    mesh.onChangedConnections(
        []() { Serial.printf("[M]: Changed Connection\n"); });
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
}

long id = 0;
int reading = 100;
void loop() {
  reading = touchRead(TOUCH_RESET_PIN);
  if (reading < 20) {
    meshReset();
  }
  if (isConnectionReady) {
    mesh.update();
    // MENGIRIM PESAN SETIAP 10 DETIK
    uint64_t now = millis();
    if (now - authCheckTime > 15000) {
      String msg = "{\"msgid\" : \"" + String(id) +
                   "\",\"type\":\"auth\",\"source\" : \"" + DATA_NODE +
                   "\",\"destination\" : \"" + DATA_GATEWAY +
                   "\",\"card\": "
                   "{\"id\" : \"4448c29FeAF0\",\"pin\" : \"123456\"}}";
      authCheckTime = millis();
      authRTOChecker = millis(); // 10
      Serial.println("[i]: Sending Request To Gateway");
      mesh.sendSingle(DATA_GATEWAY, msg);
      isWaitingForAuthResponse = true;
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

    // Jika pintu bisa dibuka dan waktu sekarang dikurang waktu pertama pintu
    // bisa
    if (isDoorOpen && millis() - doorTimestamp < DOOR_OPEN_DURATION) {
      // Lakukan Sesuatu Ketika Pintu Bisa Dibuka
      // Relay Menyala Untuk Membuka Pintu
    }

    // Jika Sudah melebihi batas waktu durasi membuka pintu maka matikan relay
    if (isDoorOpen && millis() - doorTimestamp > DOOR_OPEN_DURATION) {
      // Lakukan Sesuatu Ketika Pintu Bisa Ditutup
      // Relay Mati Pintu, Kembali terkunci
      isDoorOpen = false;
    }

    // INFO: Ketersediaan Gateway
    // Lakukan ping setiap 120 detik untuk melihat ketersediaan gateway
    if (now - connectionPingCheckTime > 40000) {
      connectionPingCheckTime = millis();
      Serial.println("[x]: Sending Connection Ping");
      String msg = "{\"type\":\"connectionping\", \"source\":\"" + DATA_NODE +
                   "\", \"destination\" : \"" + DATA_GATEWAY + +"\"}";
      mesh.sendSingle(DATA_GATEWAY, msg);
      connectionPingRTOChecker = millis();
      isWaitingForConnectionPingResponse = true;
    }

    if (isWaitingForConnectionPingResponse &&
        millis() - connectionPingRTOChecker > RTO_LIMIT) {
      Serial.println("[x]: PING RTO");
      isWaitingForConnectionPingResponse = false;
    }
  }
}