#include <Arduino.h>
//
#include "SPIFFS.h"
#include <Adafruit_GFX.h> // Core graphics library
#include <Adafruit_SSD1306.h>
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

#define LED 2
#define MESH_PORT 5555
#define RTO_LIMIT 8000           // 8s
#define DISPLAT_ALERT_LIMIT 2000 // 2s
#define DOOR_OPEN_DURATION 5000  // 5s
#define I2C_KEYPAD_ADDR 0x38
#define I2C_LCD_ADDR 0x3C
#define TOUCH 33
#define RELAY 27
#define RESET_BUTTON 25
#define LED_GATEWAY 32
// #define LED_TX 16
// #define LED_RX 17

String DATA_SSID; // Variables to save values from HTML form
String DATA_PASSWORD;
String DATA_GATEWAY;
String DEVICE_MODE = "AUTH"; // or ADMIN
String DATA_NODE;
String cardIdContainer;
String softAPIP;
String pinContainer;
String authResponsesTimeContainer = "";
String successPingResponsesTimeContainer = "";
String joinConnectionResponsesTimeContainer = "";
boolean isWaitingForAuthResponse = false;
boolean isWaitingForConnectionStartupResponse = false;
boolean isWaitingForConnectionPingResponse = false;
boolean isCheckingConnection = false;
boolean isConnectionReady = false;
boolean isResponseDestinationCorrect = false;
boolean isGatewayAvailable = false;
boolean isDoorOpen = false;
boolean isCardExist = false;
boolean isButtonPressFromInside = false;
boolean isDeviceAllowToSendAuth = false;
boolean isDisplayShowAlert = false;
boolean isDisplayShowAlertHaveLimit = false;
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
unsigned long alertTimestamp = 0;
const short RFID_RST = 4;
const short RFID_SS = 15;
const char *PARAM_INPUT_1 = "ssid"; // Search for parameter in HTTP POST request
const char *PARAM_INPUT_2 = "password";
const char *PARAM_INPUT_3 = "node";
const char *PARAM_INPUT_4 = "gateway";
const int TOUCH_RESET_PIN = 4;
const byte DISPLAY_WIDTH = 128;
const byte DISPLAY_HEIGHT = 64;
const byte ROWS = 4;
const byte COLS = 4;
// Alpha 3.1
// char keys[ROWS][COLS] = {
//     {'D', 'C', 'B', 'A'},
//     {'#', '9', '6', '3'},
//     {'0', '8', '5', '2'},
//     {'*', '7', '4', '1'},
// };
// Betta 3.2
char keys[ROWS][COLS] = {
    {'A', 'B', 'C', 'D'},
    {'3', '6', '9', '#'},
    {'2', '5', '8', '0'},
    {'1', '4', '7', '*'},
};
short responseCounter = 0;
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
Adafruit_SSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, -1);

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

void centerText(byte yLevel, byte fontSize, String text) {
  short textLength = text.length();
  byte marginTop = 5;
  byte yPos = 3;
  short xPos = ((DISPLAY_WIDTH - (textLength * fontSize * 5)) / 2) - 7;
  if (fontSize == 1) {
    marginTop = 10;
  }
  switch (yLevel) {
  case 1:
    yPos = 3;
    break;

  case 2:
    // 3 is starting y position, + font size + margin
    yPos = 3 + (fontSize * 7) + marginTop;
    break;

  case 3:
    // 3 is starting y position, + font size + margin
    yPos = 3 + (fontSize * 7) + marginTop + (fontSize * 7) + marginTop;
    break;

  default:
    yPos = 3;
    break;
  }
  display.setTextSize(fontSize);
  display.setCursor(xPos, yPos);
  display.print(text);
  // Serial.printf("Text: %s, X: %s, Y: %s", text, xPos, yPos);
}
// Reset Mesh Network
void meshReset() {
  Serial.println("[x]: Attempting To Reset ESP Mesh Network");
  String empty = "";
  writeFile(SPIFFS, ssidPath, empty.c_str());
  writeFile(SPIFFS, passwordPath, empty.c_str());
  writeFile(SPIFFS, nodePath, empty.c_str());
  writeFile(SPIFFS, gatewayPath, empty.c_str());
  centerText(2, 2, "RESET CREDENTIAL");
  delay(3000);
  ESP.restart();
}

// Check gateway connection
void meshCheckConnection() {
  connectionPingCheckTime = millis();
  Serial.println("[x]: Sending Connection Ping");
  String msg = "";
  if (joinConnectionResponsesTimeContainer != "") {
    msg = "{\"type\":\"connectionping\", \"source\":\"" + DATA_NODE +
          "\", \"destination\" : \"" + DATA_GATEWAY + +"\",\"auth\":\"" +
          authResponsesTimeContainer + "\",\"startupconnection\":\"" +
          joinConnectionResponsesTimeContainer + "\"}";
  }
  if (joinConnectionResponsesTimeContainer == "") {
    msg = "{\"type\":\"connectionping\", \"source\":\"" + DATA_NODE +
          "\", \"destination\" : \"" + DATA_GATEWAY + +"\",\"auth\":\"" +
          authResponsesTimeContainer + "\"}";
  }
  mesh.sendSingle(DATA_GATEWAY, msg);
  connectionPingRTOChecker = millis();
  isWaitingForConnectionPingResponse = true;
  authResponsesTimeContainer = ""; // reset container
  successPingResponsesTimeContainer = "";
  joinConnectionResponsesTimeContainer = "";
}

// INFO: Display Handler

void leftText(byte yLevel, byte fontSize, String text) {
  short textLength = text.length();
  byte yPos = 3;
  byte xPos = 5;
  byte marginTop = 5;
  if (fontSize == 1) {
    marginTop = 10;
  }
  yPos = 3 + (((fontSize * 7) + marginTop) * (yLevel - 1));

  display.setTextSize(fontSize);
  display.setCursor(xPos, yPos);
  display.print(text);
}

void alert(String text) {
  display.clearDisplay();
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.setTextWrap(true);
  display.setCursor(10, 10);
  display.println(text);
  display.display();
}

void displaySetUpText() {
  display.clearDisplay();
  display.setTextWrap(false);
  display.setTextColor(WHITE);
  centerText(1, 2, "WELCOME");
  centerText(2, 2, "SMART DOOR");
  centerText(3, 2, "SYSTEM");
  display.display();
}

void displayAPMode(String ip) {
  display.clearDisplay();
  display.setTextWrap(false);
  display.setTextColor(WHITE);
  centerText(1, 2, "AP MODE");
  leftText(3, 1, "ESP-MESH-MANAGER");
  leftText(4, 1, ip);
  display.display();
}

void displayWaitingConnection() {
  display.clearDisplay();
  display.setTextWrap(false);
  display.setTextColor(WHITE);
  centerText(1, 2, "TRY TO");
  centerText(2, 2, "CONN w/");
  centerText(3, 2, "GATEWAY");
  display.display();
}

void displayWritePin(String pin) {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(2);
  display.setTextWrap(false);
  if (changeMode) { // when device try to change mode
    centerText(1, 2, "ADMIN AUTH");
    display.setCursor(20, 50);
    display.setTextSize(1);
    display.println("ENTER ROOM PIN");
  }

  if (!changeMode && DEVICE_MODE == "AUTH") { // when device in auth mode
    centerText(1, 2, "SMART DOOR");
    display.setCursor(25, 50);
    display.setTextSize(1);
    display.println("TAP YOUR CARD");
  }

  if (!changeMode && DEVICE_MODE == "ADMIN") { // when device in ADMIN mode
    centerText(1, 2, "ADMIN MODE");
    leftText(2, 1, "ID: " + DATA_NODE);
    leftText(3, 1, "B: Reset Mesh");
    leftText(4, 1, "#: Admin Auth");
  }

  if ((!changeMode && DEVICE_MODE == "AUTH") || changeMode)
    if (pin.length() == 0) {
      centerText(2, 2, "------");
    } else {
      String text = "";
      for (size_t i = 0; i < pin.length(); i++) {
        text += "*";
      }

      for (size_t i = 0; i < (6 - (pin.length())); i++) {
        text += "-";
      }
      centerText(2, 2, text);
    }

  display.display();
}

void setup() {
  Serial.begin(115200);
  bootTimestamp = millis();

  // Start Instance
  initSPIFFS();
  Wire.begin();
  display.begin(SSD1306_SWITCHCAPVCC, I2C_LCD_ADDR);
  display.setRotation(0);
  displaySetUpText();

  // Pin Control
  pinMode(LED, OUTPUT);
  pinMode(RELAY, OUTPUT);
  pinMode(LED_GATEWAY, OUTPUT);
  pinMode(TOUCH, INPUT);
  pinMode(RESET_BUTTON, INPUT);
  digitalWrite(LED, LOW);
  digitalWrite(RELAY, HIGH); // Sudah di rubah
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
    String ip = "";
    for (int i = 0; i < 4; i++) {
      ip += i ? "." + String(IP[i]) : String(IP[i]);
    }

    softAPIP = ip;
    displayAPMode(ip);
    Serial.print("[x]: AP IP address: ");
    Serial.println(ip);

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
    mesh.setDebugMsgTypes(ERROR | STARTUP);
    mesh.init(DATA_SSID, DATA_PASSWORD, &userScheduler, MESH_PORT);
    mesh.setName(DATA_NODE);

    mesh.onReceive([](String &from, String &msg) {
      digitalWrite(LED, HIGH);
      Serial.printf("[i]: Get Response From Gateway: %s. %s\n", from.c_str(),
                    msg.c_str());

      // Pastikan Data Yang Diterima Memang Ditujukan Untuk Node Ini, Ubah Data
      // menjadi JSON Terlebih dahulu
      StaticJsonDocument<256> doc;
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
        doorTimestamp = millis();
        unsigned long arrivalTime = millis();
        unsigned long waitingTime = arrivalTime - authRTOChecker;
        String waitingTimeStr = String(waitingTime);
        Serial.println("[i]: Auth request start on " + String(authRTOChecker) +
                       " receive response on " + String(arrivalTime) +
                       " final response time " + String(waitingTime));
        Serial.println("[i]: Sukses Membuka Pintu");
        // if (responseCounter < 6) {
        //   authResponsesTimeContainer += waitingTimeStr + ",";
        // }
        authResponsesTimeContainer += waitingTimeStr + ",";
        responseCounter += 1;
        isWaitingForAuthResponse = false;     // reset value
        isResponseDestinationCorrect = false; // reset value
        isDoorOpen = true;
        isWaitingForAuthResponse = false; // cleare alert
      }

      if (isWaitingForAuthResponse && isResponseDestinationCorrect &&
          doc["success"] == false && type == "auth") {
        isWaitingForAuthResponse = false;     // reset value
        isResponseDestinationCorrect = false; // reset value
        isDoorOpen = false;
        Serial.println("[x]: Failed To Open Room");
        alert("FAILED\n OPEN\n ROOM");
        isDisplayShowAlert = true;
        isDisplayShowAlertHaveLimit = true;
        String waitingTimeStr = String(millis() - authRTOChecker);
        authResponsesTimeContainer += waitingTimeStr + ",";
        isDoorOpen = false; // jika sistem ingin langsung mengunci pintu jika
                            // kartu tidak valid
        alertTimestamp = millis(); // prepare to reset display
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
        String waitingTimeStr = String(millis() - bootTimestamp);
        joinConnectionResponsesTimeContainer += waitingTimeStr + ",";
        isWaitingForConnectionStartupResponse = false; // reset value
        isResponseDestinationCorrect = false;          // reset value
        isConnectionReady = true; // allow device to operate
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
        String waitingTimeStr = String(waitingTime);
        successPingResponsesTimeContainer += waitingTimeStr + ",";
        isGatewayAvailable = true; // alllow user to tap their card
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
      if (now > bootTimestamp + 5000) {
        displayWaitingConnection();
      }
    }

    // INFO: RESET BUTTON
    if (digitalRead(RESET_BUTTON) == HIGH) {
      meshReset();
    }
  }

  if (isConnectionReady && APStatus == false) {
    Serial.println("[x]: Waking Up I2C & Keypad");
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
  // INFO: TUCH BUTTON
  // Serial.println(analogRead(TOUCH));
  if (analogRead(TOUCH) > 1000) {
    isDoorOpen = true;
    doorTimestamp = millis();
    isButtonPressFromInside = true;
  }

  // INFO: RESET BUTTON
  if (digitalRead(RESET_BUTTON) == HIGH) {
    meshReset();
  }

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

    // Check Gateway Connection
    if (key == '*') {
      meshCheckConnection();
      alert("CHECK\n GATEWAY\n CONN..");
      alertTimestamp = millis();
      isDisplayShowAlert = true;
      isCheckingConnection = true;
    }

    // Authenticate To Change Device Mode
    if (key == 'D' && changeMode) {
      if (pinContainer == DEVICE_SEC_PIN) {
        if (DEVICE_MODE == "AUTH") {
          DEVICE_MODE = "ADMIN";
        } else {
          DEVICE_MODE = "AUTH";
        }
        changeMode = false;
        Serial.printf("[x]: Success Changing Mode to %s\n", DEVICE_MODE);
      }

      if (pinContainer != DEVICE_SEC_PIN) {
        Serial.println("[x]: Failed to success Changing Mode");
        alert("FAILED\n TO CHANGE\n MODE");
        isDisplayShowAlert = true;
        isDisplayShowAlertHaveLimit = true;
        alertTimestamp = millis(); // prepare to reset display
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
      alert(" \n LOADING\n ");
      isDisplayShowAlert = true;
    }

    if (isWaitingForAuthResponse && millis() - authRTOChecker > RTO_LIMIT) {
      Serial.println("[i]: AUTH RTO");
      isWaitingForAuthResponse = false;
      alert("AUTH\n RTO\n ");
      alertTimestamp = millis(); // prepare to reset display
      isDisplayShowAlert = true;
      isDisplayShowAlertHaveLimit = true;
    }

    // if (isWaitingForAuthResponse) {
    //   // Lakukan Sesuatu Ketika Sedang Menunggu Response
    //   Serial.println("[x]: Waiting...");
    // }

    // INFO: Ketersediaan Gateway
    // Lakukan ping setiap 60 detik untuk melihat ketersediaan gateway
    if (now - connectionPingCheckTime > 60000) {
      meshCheckConnection();
    }

    // Jika Ping tidak berbalas
    if (isWaitingForConnectionPingResponse &&
        millis() - connectionPingRTOChecker > RTO_LIMIT) {
      Serial.println("[x]: PING RTO");
      isWaitingForConnectionPingResponse = false;
      isGatewayAvailable = false; // reset gateway response
      digitalWrite(LED_GATEWAY, LOW);
      if (isCheckingConnection) {
        alert("GATEWAY\n NOT\n AVAILABLE");
        alertTimestamp = millis();
        isDisplayShowAlert = true;
        isDisplayShowAlertHaveLimit = true;
        isCheckingConnection = false;
      }
    }

    // Jika User Sedang Memeriksa Konksi dan berhasil
    if (isCheckingConnection && isGatewayAvailable) {
      digitalWrite(LED_GATEWAY, HIGH);
      alert("GATEWAY\n AVAILABLE\n ");
      alertTimestamp = millis();
      isDisplayShowAlert = true;
      isDisplayShowAlertHaveLimit = true;
      isWaitingForConnectionPingResponse = false;
      isCheckingConnection = false;
      isGatewayAvailable = false; // reset gateway response
    }
  }

  // INFO: magnetic lock handler
  // Jika pintu bisa dibuka dan waktu sekarang dikurang waktu pertama perintah
  // untuk membuka pintu
  if (isDoorOpen && millis() - doorTimestamp < DOOR_OPEN_DURATION) {
    // Lakukan Sesuatu Ketika Pintu Bisa Dibuka
    // Relay Menyala Untuk Membuka Pintu
    digitalWrite(RELAY, LOW); // Sudah Dirubah
    if (isButtonPressFromInside) {
      alert("OPEN\n FROM\n INSIDE");
    } else {
      alert("SUCCESS\n OPEN \n ROOM");
    }

    isDisplayShowAlert = true;
  }

  // Jika Sudah melebihi batas waktu durasi membuka pintu maka matikan relay
  if (isDoorOpen && millis() - doorTimestamp > DOOR_OPEN_DURATION) {
    // Lakukan Sesuatu Ketika Pintu Bisa Ditutup
    // Relay Mati Pintu, Kembali terkunci
    digitalWrite(RELAY, HIGH); // Sudah Dirubah
    isDoorOpen = false;
    isDisplayShowAlert = false;
    isButtonPressFromInside = false;
  }

  if (isDoorOpen == false) {
    digitalWrite(RELAY, HIGH); // Sudah Dirubah
  }

  // INFO: Display Handler
  if (isDisplayShowAlert == false && APStatus == false) {
    displayWritePin(pinContainer);
  }

  if (isDisplayShowAlert == false && APStatus == true) {
    displayAPMode(softAPIP);
  }

  if (millis() > alertTimestamp + DISPLAT_ALERT_LIMIT &&
      isDisplayShowAlertHaveLimit == true) {
    isDisplayShowAlert = false;
    isDisplayShowAlertHaveLimit = false;
  }
}