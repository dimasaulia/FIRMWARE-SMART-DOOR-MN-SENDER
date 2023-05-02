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
#define RTO_LIMIT 10000         // 10s
#define DOOR_OPEN_DURATION 5000 // 5s

String NODE_ID = "EkXeg";
String NODE_FULL_NAME = "NODE-" + NODE_ID;
String NODE_DESTINATION = "GATEWAY-nkXgI";
String form_ssid; // Variables to save values from HTML form
String form_password;
String form_gateway;
String form_node;
boolean isWaitingForAuthResponse = false;
boolean isWaitingForConnectionStartupResponse = false;
boolean isConnectionReady = false;
boolean isResponseDestinationCorrect = false;
boolean isDoorOpen = false;
unsigned long authCheckTime = 0;
unsigned long connectionStartupCheckTime = 0;
unsigned long authRTOChecker = 0;
unsigned long connectionStartupRTOChecker = 0;
unsigned long doorTimestamp = 0;
const char *PARAM_INPUT_1 = "ssid"; // Search for parameter in HTTP POST request
const char *PARAM_INPUT_2 = "password";
const char *PARAM_INPUT_3 = "node";
const char *PARAM_INPUT_4 = "gateway";
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
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
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
bool initMesh() {
  if (form_ssid == "" || form_password == "" || form_gateway == "" ||
      form_node == "") {
    Serial.println("Undefined SSID, Password, Gateway, Node Value.");
    return false;
  }

  return true;
}

void setup() {
  Serial.begin(115200);
  initSPIFFS();

  // Pin Controll
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  // read variable
  form_ssid = readFile(SPIFFS, ssidPath);
  form_password = readFile(SPIFFS, passwordPath);
  form_node = readFile(SPIFFS, nodePath);
  form_gateway = readFile(SPIFFS, gatewayPath);
  Serial.print("SSID: ");
  Serial.println(form_ssid);
  Serial.print("Password: ");
  Serial.println(form_password);
  Serial.print("Node: ");
  Serial.println(form_node);
  Serial.print("Gateway: ");
  Serial.println(form_gateway);

  if (initMesh() == false) {
    // Connect to ESP Mesh network with SSID and password
    Serial.println("Setting AP (Access Point)");
    // NULL sets an open Access Point
    WiFi.softAP("ESP-MESH-MANAGER", NULL);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    // Web Server Root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(SPIFFS, "/index.html", "text/html");
    });
    server.serveStatic("/", SPIFFS, "/");

    // Handling POST Request
    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
      int params = request->params();
      for (int i = 0; i < params; i++) {
        AsyncWebParameter *p = request->getParam(i);
        if (p->isPost()) {
          // HTTP POST ssid value
          if (p->name() == PARAM_INPUT_1) {
            form_ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(form_ssid);
            // Write file to save value
            writeFile(SPIFFS, ssidPath, form_ssid.c_str());
          }
          // HTTP POST password value
          if (p->name() == PARAM_INPUT_2) {
            form_password = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(form_password);
            // Write file to save value
            writeFile(SPIFFS, passwordPath, form_password.c_str());
          }
          // HTTP POST Node value
          if (p->name() == PARAM_INPUT_3) {
            form_node = p->value().c_str();
            Serial.print("Node value set to: ");
            Serial.println(form_node);
            // Write file to save value
            writeFile(SPIFFS, nodePath, form_node.c_str());
          }
          // HTTP POST gateway value
          if (p->name() == PARAM_INPUT_4) {
            form_gateway = p->value().c_str();
            Serial.print("Gateway set to: ");
            Serial.println(form_gateway);
            // Write file to save value
            writeFile(SPIFFS, gatewayPath, form_gateway.c_str());
          }
          // Serial.printf("POST[%s]: %s\n", p->name().c_str(),
          // p->value().c_str());
        }
      }
      request->send(200, "text/plain",
                    "Done. ESP will restart, connect to ESP Mesh Network  ");
      delay(3000);
      ESP.restart();
    });

    server.begin();
  }

  if (initMesh()) {
    Serial.println("Trying to connect to esp mesh");
    mesh.setDebugMsgTypes(ERROR | STARTUP);
    mesh.init(form_ssid, form_password, &userScheduler, MESH_PORT);
    mesh.setName(form_node);

    mesh.onReceive([](String &from, String &msg) {
      digitalWrite(LED, HIGH);
      Serial.printf("[i]: Get Response From Gateway: %s. %s\n", from.c_str(),
                    msg.c_str());

      // FOR AUTH STATISTIC PURPOSE
      if (isWaitingForAuthResponse) {
        isWaitingForAuthResponse = false; // 11 - 10 = 1
        unsigned long waitingTime = millis() - authRTOChecker;
        Serial.println("[i]: Auth Response time: " + String(waitingTime));
      }

      // Pastikan Data Yang Diterima Memang Ditujukan Untuk Node Ini, Ubah Data
      // menjadi JSON Terlebih dahulu
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, msg.c_str());
      if (error) {
        Serial.println("Failed to deserializeJson");
        Serial.println(error.f_str());
        return;
      }

      // memastikan data yang ditujukan sesuai,
      // *opsional untuk melakukan pengecekan dari mana sumber data yang
      // diterima
      if (doc["destination"] == form_node) {
        isResponseDestinationCorrect = true;
      }

      // Jika tipe response yang diterima adalah "auth"
      // Melihat response apakah pintu bisa dibuka
      if (isResponseDestinationCorrect && doc["success"] == true &&
          doc["type"] == "auth") {
        isResponseDestinationCorrect = false; // reset value
        isDoorOpen = true;
        doorTimestamp = millis();
        Serial.println("Sukses Membuka Pintu");
      }

      // Jika response yang diterima adalah "connectionstartup"
      if (isWaitingForConnectionStartupResponse) {
        unsigned long waitingTime = millis() - connectionStartupRTOChecker;
        Serial.println("Connection Ready in " + String(waitingTime) + " ms");
      }

      if (isResponseDestinationCorrect && doc["success"] == true &&
          doc["type"] == "connectionstartup") {
        Serial.println("DEVICE READY");
        isResponseDestinationCorrect = false;          // reset value
        isWaitingForConnectionStartupResponse = false; // reset value
        isConnectionReady = true; // allow device to operate
      }
      digitalWrite(LED, LOW);
    });

    mesh.onChangedConnections(
        []() { Serial.printf(" [M]: Changed Connection\n"); });
  }
  // When First Start Up try to connect to gateway
  while (isConnectionReady == false) {
    mesh.update();
    // MENGIRIM PESAN SETIAP 5 DETIK
    uint64_t now = millis();
    if (now - connectionStartupCheckTime > 5000) {
      Serial.println("Sending Connection Startup");
      connectionStartupCheckTime = millis();
      String msg = "{\"type\":\"connectionstartup\", \"source\":\"" +
                   NODE_FULL_NAME + "\", \"destination\" : \"" +
                   NODE_DESTINATION + +"\"}";
      mesh.sendSingle(form_gateway, msg);
      isWaitingForConnectionStartupResponse = true;
      connectionStartupRTOChecker = millis();
    }

    /* code */
  }
}

long id = 0;
void loop() {
  if (isConnectionReady) {
    mesh.update();
    // MENGIRIM PESAN SETIAP 20 DETIK
    uint64_t now = millis();
    if (now - authCheckTime > 20000) {
      authCheckTime = millis();
      String msg = "{\"msgid\" : \"" + String(id) +
                   "\",\"type\":\"auth\",\"source\" : \"" + NODE_FULL_NAME +
                   "\",\"destination\" : \"" + NODE_DESTINATION +
                   "\",\"card\": "
                   "{\"id\" : \"4448c29FeAF0\",\"pin\" : \"123456\"}}";
      Serial.println("[i]: Sending Request To Gateway");
      mesh.sendSingle(form_gateway, msg);
      authRTOChecker = millis(); // 10
      isWaitingForAuthResponse = true;
      id++;
    }

    if (isWaitingForAuthResponse && millis() - authRTOChecker > RTO_LIMIT) {
      Serial.println(" [M]: RTO");
      isWaitingForAuthResponse = false;
    }

    // if (isWaitingForAuthResponse) {
    // Lakukan Sesuatu Ketika Sedang Menunggu Response
    //   Serial.println(" [M]: Waiting...");
    // }

    // Jika pintu bisa dibuka dan waktu sekarang dikurang waktu pertama pintu
    // bisa
    if (isDoorOpen && millis() - doorTimestamp < DOOR_OPEN_DURATION) {
      // Lakukan Sesuatu Ketika Pintu Bisa Dibuka
      // Relay Menyala Untuk Membuka Pintu
    }

    // Jika Sudah melbihi batas waktu durasi membuka pintu maka matikan relay
    if (isDoorOpen && millis() - doorTimestamp > DOOR_OPEN_DURATION) {
      // Lakukan Sesuatu Ketika Pintu Bisa Ditutup
      // Relay Mati Pintu, Kembali terkunci
      isDoorOpen = false;
    }
  }
}