/*
  Project: CARBERUS - Smart Key Firmware (FINAL OPTIMIZED)
  Hardware: Arduino Nano ESP32 + AS608 Fingerprint Sensor
  Features: AES-128, Rolling Code, Burst Transmission (Battery Saver)
*/

#include <ArduinoBLE.h>
#include <Adafruit_Fingerprint.h>
#include "mbedtls/aes.h"

// ================================================================
// DATA STRUCTURES (Must be declared before use)
// ================================================================
struct SecurePacket {
  uint32_t deviceID;
  uint32_t counter;
  uint8_t command;
  uint8_t padding[7]; 
};

// ================================================================
// CONFIGURATION
// ================================================================
// Nano ESP32: D0=RX, D1=TX (Connected to Sensor Green/White)
const int PIN_RX = D0; 
const int PIN_TX = D1;

// --- BLE UUIDs ---
BLEService managementService("AAA0"); 
BLEStringCharacteristic statusChar("AAA1", BLERead | BLENotify, 30);
BLEStringCharacteristic commandChar("AAA2", BLEWrite, 20);

// --- AES KEY (Must match Raspberry Pi) ---
uint8_t aes_key[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                     0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

HardwareSerial mySerial(1);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// Rolling Code Counter
uint32_t rollingCounter = 0; 
bool maintenanceMode = false;

// ================================================================
// MAIN SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  
  // Initialize Sensor
  mySerial.begin(57600, SERIAL_8N1, PIN_RX, PIN_TX);
  finger.begin(57600);
  delay(100);
  
  if (finger.verifyPassword()) {
    Serial.println("[KEY] Biometric Sensor: ONLINE");
    digitalWrite(LED_BUILTIN, HIGH); delay(200); digitalWrite(LED_BUILTIN, LOW);
  } else {
    Serial.println("[ERR] Sensor not found! Check wiring.");
    while(1) { digitalWrite(LED_BUILTIN, HIGH); delay(100); digitalWrite(LED_BUILTIN, LOW); delay(100); }
  }

  // Initialize BLE
  if (!BLE.begin()) {
    Serial.println("[ERR] BLE Start Failed");
    while(1);
  }

  // Advertise for App connection
  setupAppAdvertising();
  Serial.println("[SYS] System Ready. Waiting for Fingerprint or App connection...");
}

// ================================================================
// MAIN LOOP
// ================================================================
void loop() {
  BLE.poll();

  if (!maintenanceMode) {
     int id = getFingerprintID();
     
     if (id > 0) {
        Serial.print("[BIO] Finger ID #"); Serial.print(id); Serial.println(" recognized.");
        executeUnlockSequenceBroadcast();
        setupAppAdvertising();
     }
  }
  delay(50);
}

// ================================================================
// BLE & SECURITY FUNCTIONS (OPTIMIZED)
// ================================================================

void setupAppAdvertising() {
  BLE.stopAdvertise(); 
  BLE.setLocalName("Carberus_Key_Manager");
  BLE.setAdvertisedService(managementService);
  
  if (managementService.characteristicCount() == 0) {
     managementService.addCharacteristic(statusChar);
     managementService.addCharacteristic(commandChar);
     BLE.addService(managementService);
     commandChar.setEventHandler(BLEWritten, onCommandReceived);
  }
  BLE.advertise();
}

void executeUnlockSequenceBroadcast() {
  Serial.println(">>> BURST TRANSMISSION (OPTIMIZED) <<<");
  digitalWrite(LED_BUILTIN, HIGH);
  
  BLE.stopAdvertise(); 

  rollingCounter++; 
  
  // Create Packet
  SecurePacket packet;
  packet.deviceID = 0xCAFEBABE; 
  packet.counter = rollingCounter;
  packet.command = 1; // UNLOCK
  for(int i=0; i<7; i++) packet.padding[i] = (uint8_t)random(0, 255);

  // Encrypt
  unsigned char inputBuffer[16];
  unsigned char outputBuffer[16];
  memcpy(inputBuffer, &packet, 16);
  
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, aes_key, 128);
  mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, inputBuffer, outputBuffer);
  mbedtls_aes_free(&aes);

  // Set Data
  BLEAdvertisingData advData;
  advData.setManufacturerData(0xFFFF, outputBuffer, 16);
  BLE.setAdvertisingData(advData);

  // BURST TRANSMIT (600ms)
  BLE.advertise();
  delay(600); 
  BLE.stopAdvertise();
  
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println(">>> TX END <<<");
  delay(1000); // Debounce
}

// ================================================================
// APP COMMAND HANDLER
// ================================================================
void onCommandReceived(BLEDevice central, BLECharacteristic characteristic) {
  String cmd = commandChar.value();
  Serial.print("[APP] Command: "); Serial.println(cmd);
  
  int separatorIndex = cmd.indexOf(':');
  if (separatorIndex == -1) return;

  String action = cmd.substring(0, separatorIndex);
  int id = cmd.substring(separatorIndex + 1).toInt();

  if (id <= 0 || id > 127) {
    statusChar.writeValue("Err: Invalid ID");
    return;
  }

  if (action == "ENROLL") {
    maintenanceMode = true; 
    startEnrollmentProcess(id);
    maintenanceMode = false; 
  } 
  else if (action == "DELETE") {
    maintenanceMode = true;
    deleteFinger(id);
    maintenanceMode = false;
  }
}

void deleteFinger(int id) {
  statusChar.writeValue("Deleting...");
  if (finger.deleteModel(id) == FINGERPRINT_OK) {
    statusChar.writeValue("SUCCESS! Deleted.");
  } else {
    statusChar.writeValue("Err: Delete Failed.");
  }
}

void startEnrollmentProcess(int id) {
  statusChar.writeValue("Place finger...");
  int p = -1;
  while (p != FINGERPRINT_OK) { p = finger.getImage(); BLE.poll(); } 
  finger.image2Tz(1);
  
  statusChar.writeValue("Remove finger...");
  delay(2000);
  while (p != FINGERPRINT_NOFINGER) { p = finger.getImage(); }

  statusChar.writeValue("Place same finger...");
  p = -1;
  while (p != FINGERPRINT_OK) { p = finger.getImage(); BLE.poll(); } 
  finger.image2Tz(2);

  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    if (finger.storeModel(id) == FINGERPRINT_OK) {
      statusChar.writeValue("ENROLLMENT SUCCESS!");
      for(int i=0; i<3; i++) { digitalWrite(LED_BUILTIN, HIGH); delay(200); digitalWrite(LED_BUILTIN, LOW); delay(200); }
    } else {
      statusChar.writeValue("Err: Storage Error");
    }
  } else {
    statusChar.writeValue("Err: Mismatch");
  }
}

int getFingerprintID() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -1;
  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;
  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) return finger.fingerID;
  return -1;
}