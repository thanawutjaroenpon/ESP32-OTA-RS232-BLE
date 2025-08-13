#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define RXD2 16
#define TXD2 17

const char* ssid = "ESP32-AP";
const char* password = "12345678";

WebServer server(80);

BLECharacteristic *pTxCharacteristic;
BLECharacteristic *pRxCharacteristic;

bool deviceConnected = false;
bool streamingReal = false;
bool streamingFixed = false;
bool wifiStarted = false;

void setupOTA() {
  WiFi.softAP(ssid, password);
  Serial.println("WiFi AP started for OTA");

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", R"rawliteral(
      <!DOCTYPE html>
      <html>
      <head>
        <title>ESP32 OTA Update</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <style>
          body {
            font-family: Arial, sans-serif;
            background-color: #f3f4f6;
            display: flex;
            align-items: center;
            justify-content: cs    enter;
            height: 100vh;
            margin: 0;
          }
          .container {
            background-color: #fff;
            padding: 2rem;
            border-radius: 12px;
            box-shadow: 0 4px 20px rgba(0,0,0,0.1);
            max-width: 400px;
            width: 90%;
            text-align: center;
          }
          h2 {
            margin-bottom: 1rem;
            color: #333;
          }
          input[type='file'] {
            margin: 1rem 0;
          }
          input[type='submit'] {
            background-color: #2563eb;
            color: white;
            padding: 0.5rem 1.2rem;
            border: none;
            border-radius: 6px;
            cursor: pointer;
            font-size: 1rem;
          }
          input[type='submit']:hover {
            background-color: #1d4ed8;
          }
          .footer {
            margin-top: 1rem;
            font-size: 0.8rem;
            color: #888;
          }
        </style>
      </head>
      <body>
        <div class="container">
          <h2>ESP32 OTA Update</h2>
          <form method="POST" action="/update" enctype="multipart/form-data">
            <input type="file" name="update" required><br>
            <input type="submit" value="Upload & Update">
          </form>
          <div class="footer">BLE + WiFi OTA Mode</div>
        </div>
      </body>
      </html>
    )rawliteral");
  });

  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", Update.hasError() ? "Update Failed!" : "Update Success! Rebooting...");
    delay(3000);
    ESP.restart();
  }, []() {
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.println("OTA upload started");
      Update.begin();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      Update.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.println("OTA update finished");
      } else {
        Serial.printf("OTA update error: %s\n", Update.errorString());
      }
    }
  });

  server.begin();
  wifiStarted = true;
}

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("BLE Client Connected");
  }

  void onDisconnect(BLEServer* pServer) override {
    Serial.println("BLE Client Disconnected");

    deviceConnected = false;
    streamingReal = false;
    streamingFixed = false;

    if (wifiStarted) {
      server.stop();
      WiFi.softAPdisconnect(true);
      wifiStarted = false;
      Serial.println("OTA WiFi stopped");
    }

    // Restart BLE advertising so client can reconnect
    pServer->getAdvertising()->start();
    Serial.println("BLE Advertising restarted");

    Serial.println("System reset to initial state (no reboot)");
  }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String cmd = String(pCharacteristic->getValue().c_str());
    if (cmd.length() > 0) {
      cmd.trim();

      Serial.print("BLE Command received: ");
      Serial.println(cmd);

      if (cmd.equalsIgnoreCase("GetW")) {
        streamingReal = false;
        streamingFixed = false;
        String response = "WT:1234.56 TR:00123.45 OK\r\n";
        if (deviceConnected) {
          pTxCharacteristic->setValue(response.c_str());
          pTxCharacteristic->notify();
          Serial.println("Sent fixed test response for GetW");
        }

      } else if (cmd.equalsIgnoreCase("GetRT")) {
        streamingReal = false;
        streamingFixed = true;
        Serial.println("Fixed streaming STARTED");

      } else if (cmd.equalsIgnoreCase("GetR")) {
        streamingFixed = false;
        streamingReal = true;
        Serial.println("Real data streaming STARTED");

      } else if (cmd.equalsIgnoreCase("Stop")) {
        streamingReal = false;
        streamingFixed = false;
        Serial.println("Streaming STOPPED");
        if (deviceConnected) {
          pTxCharacteristic->setValue("Streaming Stopped\r\n");
          pTxCharacteristic->notify();
        }

      } else if (cmd.equalsIgnoreCase("Config")) {
        if (!wifiStarted) {
          setupOTA();
          if (deviceConnected) {
            pTxCharacteristic->setValue("OTA Mode Enabled\r\n");
            pTxCharacteristic->notify();
          }
        }

      } else {
        // Forward unknown command to MAX3232 serial
        Serial2.println(cmd);
      }
    }
  }
};

void setupBLE() {
  BLEDevice::init("ESP32-BLE-Serial");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");

  pTxCharacteristic = pService->createCharacteristic(
    "6E400003-B5A3-F393-E0A9-E50E24DCCA9E",
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());

  pRxCharacteristic = pService->createCharacteristic(
    "6E400002-B5A3-F393-E0A9-E50E24DCCA9E",
    BLECharacteristic::PROPERTY_WRITE
  );
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();
  pServer->getAdvertising()->start();

  Serial.println("BLE UART started and advertising");
}

void sendFixedData() {
  const char* fixedMessage = "W:45.00\r\n";
  if (deviceConnected) {
    pTxCharacteristic->setValue(fixedMessage);
    pTxCharacteristic->notify();
    Serial.print("Fixed data sent: ");
    Serial.println(fixedMessage);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== ESP32 BLE + OTA Boot ===");

  Serial2.begin(9600, SERIAL_7E1, RXD2, TXD2);
  setupBLE();
}

void loop() {
  if (wifiStarted) {
    server.handleClient();
  }

  if (streamingReal && Serial2.available()) {
    String data = Serial2.readStringUntil('\n');
    data.trim();
    if (deviceConnected && data.length() > 0) {
      String response = "WT:" + data + " OK\r\n";
      pTxCharacteristic->setValue(response.c_str());
      pTxCharacteristic->notify();
      Serial.print("Real data sent: ");
      Serial.println(response);
    }
  }

  if (streamingFixed) {
    sendFixedData();
  }
}
