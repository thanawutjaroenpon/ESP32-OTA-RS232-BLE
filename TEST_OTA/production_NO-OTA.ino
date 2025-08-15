#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define RXD2 16
#define TXD2 17

BLEServer* pServer;
BLECharacteristic* pTxCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;

#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define TX_CHAR_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define RX_CHAR_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("BLE Connected");
  }
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("BLE Disconnected");
  }
};

void setup() {
  Serial.begin(9600);              
  Serial2.begin(9600, SERIAL_7E1, RXD2, TXD2);  

  BLEDevice::init("ESP32_MAX3232");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  pTxCharacteristic = pService->createCharacteristic(
    TX_CHAR_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());

  pService->createCharacteristic(
    RX_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();

  Serial.println("BLE + MAX3232 bridge ready (Receive Only)");
}

void loop() {
  if (!deviceConnected) {
    delay(100);
    return;
  }

  // อ่านข้อมูลจาก MAX3232 (Serial2) และส่งไป BLE
  while (Serial2.available()) {
    String incoming = Serial2.readStringUntil('\n');
    incoming += "\n";
    pTxCharacteristic->setValue(incoming.c_str());
    pTxCharacteristic->notify();
    Serial.print("Forwarded to BLE: ");
    Serial.println(incoming);
  }

  delay(50); // ลดความหน่วงให้รับข้อมูลได้ทัน

  // ตรวจจับ reconnect / disconnect
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("Restart advertising for reconnect");
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    Serial.println("Reconnected");
    oldDeviceConnected = deviceConnected;
  }
}
