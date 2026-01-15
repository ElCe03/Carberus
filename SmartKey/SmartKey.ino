/*
  Project: CARBERUS - Smart Key Firmware (FINAL STABLE)
  Hardware: Arduino Nano ESP32 + AS608 Fingerprint Sensor
  Features: 
    - AES-128 Encrypted Broadcast
    - Rolling Code with Persistent Storage
    - Non-Blocking App Management (State Machine)
    - UI Synchronization Fixes (Solved "Requesting..." hang)
*/

#include <ArduinoBLE.h>
#include <Adafruit_Fingerprint.h>
#include "mbedtls/aes.h"
#include <Preferences.h>

// ================================================================
// DATA STRUCTURES & GLOBALS
// ================================================================

// Packet structure for AES encryption
struct SecurePacket {
  uint32_t deviceID;
  uint32_t counter;
  uint8_t command;
  uint8_t padding[7]; 
};

// State Machine States for Asynchronous Command Handling
enum AppCommand { CMD_NONE, CMD_ENROLL, CMD_DELETE };
volatile AppCommand pendingCommand = CMD_NONE; // Currently pending command
volatile int pendingID = 0;                    // Target ID for the command

// --- CONFIGURATION ---
// Arduino Nano ESP32 Hardware Serial Pins (D0/D1)
const int PIN_RX = D0; 
const int PIN_TX = D1;
const unsigned long ENROLL_TIMEOUT_MS = 15000; // 15 Seconds Timeout

// --- BLE UUIDs ---
BLEService managementService("AAA0"); 
BLEStringCharacteristic statusChar("AAA1", BLERead | BLENotify, 30); // Notifications to App
BLEStringCharacteristic commandChar("AAA2", BLEWrite, 20);           // Commands from App

// --- AES KEY (Must match the Receiver/Raspberry Pi) ---
uint8_t aes_key[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                     0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

// Hardware Objects
HardwareSerial mySerial(1);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
Preferences preferences; // Persistent memory handler

// Logic Variables
uint32_t rollingCounter = 0; 

// ================================================================
// MAIN SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  
  // 1. Initialize Permanent Memory (Restore Rolling Code)
  preferences.begin("carberus", false); 
  rollingCounter = preferences.getUInt("counter", 0);
  Serial.print("[SYS] Booting... Last Counter: "); Serial.println(rollingCounter);

  // 2. Initialize Fingerprint Sensor
  mySerial.begin(57600, SERIAL_8N1, PIN_RX, PIN_TX);
  finger.begin(57600);
  delay(100);
  
  if (finger.verifyPassword()) {
    Serial.println("[KEY] Sensor: ONLINE");
    blinkLED(1);
  } else {
    Serial.println("[ERR] Sensor failure or wiring error");
    blinkLED(5); // Visual error feedback
  }

  // 3. Initialize BLE
  if (!BLE.begin()) {
    Serial.println("[ERR] BLE Init Failed");
    while(1);
  }

  setupAppAdvertising();
  Serial.println("[SYS] System Ready. Loop started.");
}

// ================================================================
// MAIN LOOP
// ================================================================
void loop() {
  // 1. Keep BLE stack alive (Critical)
  BLE.poll(); 

  // 2. PENDING COMMAND HANDLING (State Machine)
  // We execute the heavy operations HERE, outside the BLE callback.
  if (pendingCommand != CMD_NONE) {
    
    if (pendingCommand == CMD_ENROLL) {
      startEnrollmentProcess(pendingID);
    } 
    else if (pendingCommand == CMD_DELETE) {
      deleteFinger(pendingID);
    }
    
    // Reset state after execution
    pendingCommand = CMD_NONE;
    pendingID = 0;
  }

  // 3. NORMAL OPERATION (Unlock Mode)
  // Only scan for unlock if we are NOT handling app commands
  if (pendingCommand == CMD_NONE) {
     int id = getFingerprintID();
     if (id > 0) {
        Serial.printf("[BIO] ID %d recognized.\n", id);
        executeUnlockSequenceBroadcast();
        setupAppAdvertising(); // Restart advertising after burst
     }
  }
  
  delay(50); // Stability delay
}

// ================================================================
// BLE CALLBACK (NON-BLOCKING WITH IMMEDIATE ACK)
// ================================================================
void onCommandReceived(BLEDevice central, BLECharacteristic characteristic) {
  String cmd = commandChar.value();
  Serial.print("[APP] RX CMD: "); Serial.println(cmd);
  
  // Parse Command (Format: "ACTION:ID")
  int separatorIndex = cmd.indexOf(':');
  if (separatorIndex == -1) return;

  String action = cmd.substring(0, separatorIndex);
  int id = cmd.substring(separatorIndex + 1).toInt();

  if (id <= 0 || id > 127) {
    statusChar.writeValue("Err: Invalid ID");
    return;
  }

  // --- CRITICAL UI FIX ---
  // Send immediate feedback to App to clear "Requesting..." state
  statusChar.writeValue("Processing...");
  
  // Flag the intent and return immediately to free BLE stack
  pendingID = id;
  
  if (action == "ENROLL") {
    pendingCommand = CMD_ENROLL;
  } 
  else if (action == "DELETE") {
    pendingCommand = CMD_DELETE;
  }
}

// ================================================================
// LOGIC FUNCTIONS (ENROLLMENT)
// ================================================================

// Helper to wait for finger with optimized polling
int waitForFingerSafe(bool fingerDetectedCondition) {
  int p = -1;
  unsigned long startTime = millis();
  
  // Clear any garbage from serial buffer
  while (mySerial.available()) { mySerial.read(); }

  Serial.println(fingerDetectedCondition ? "[BIO] Waiting for Finger..." : "[BIO] Waiting for removal...");

  while (true) {
    // 1. OPTIMIZED BLE POLLING
    // Do not poll every microsecond, do it every 50ms to save CPU for Sensor
    if ((millis() % 50) == 0) { 
      BLE.poll(); 
    }

    // 2. READ SENSOR
    p = finger.getImage();

    // 3. EXIT CONDITIONS (Success)
    if (fingerDetectedCondition && p == FINGERPRINT_OK) return FINGERPRINT_OK;
    if (!fingerDetectedCondition && p == FINGERPRINT_NOFINGER) return FINGERPRINT_NOFINGER;

    // 4. ERROR DIAGNOSTICS (Optional debug)
    if (p != FINGERPRINT_OK && p != FINGERPRINT_NOFINGER) {
       // Only print occasionally to avoid log flooding
       if ((millis() % 1000) == 0) { 
         Serial.print("[BIO] Sensor State Code: "); Serial.println(p); 
       }
    }

    // 5. TIMEOUT
    if (millis() - startTime > ENROLL_TIMEOUT_MS) {
      Serial.println("[BIO] TIMEOUT EXPIRED!");
      return -99; 
    }
    
    delay(10); // Stability delay
  }
}

void startEnrollmentProcess(int id) {
  Serial.println("[ENROLL] Starting Sequence for ID: " + String(id));

  // --- PREPARATION (UI FIX) ---
  // Send an intermediate message to "wake up" the App UI
  statusChar.writeValue("Init Sensor...");
  delay(200); // Give App time to process

  // --- STEP 1: FIRST IMAGE ---
  statusChar.writeValue("Place finger...");
  delay(150); // Critical delay for notification delivery
  
  int result = waitForFingerSafe(true); 
  
  if (result == -99) { 
    statusChar.writeValue("Timeout: Retry"); 
    return; 
  }
  if (result != FINGERPRINT_OK) { 
    statusChar.writeValue("Err: Sensor"); 
    return; 
  }

  finger.image2Tz(1);
  
  // --- STEP 2: REMOVAL ---
  statusChar.writeValue("Remove finger...");
  delay(150); // UI Delay
  
  result = waitForFingerSafe(false);
  
  if (result == -99) { 
    statusChar.writeValue("Timeout: Remove!"); 
    return; 
  }
  
  Serial.println("[ENROLL] Finger removed. Waiting 2s...");
  delay(2000); // Mandatory delay for user experience

  // --- STEP 3: SECOND IMAGE ---
  statusChar.writeValue("Place same finger...");
  delay(150); // UI Delay
  
  result = waitForFingerSafe(true);
  
  if (result == -99) { 
    statusChar.writeValue("Timeout: Retry"); 
    return; 
  }
  
  finger.image2Tz(2);

  // --- STEP 4: STORAGE ---
  int p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    if (finger.storeModel(id) == FINGERPRINT_OK) {
      statusChar.writeValue("ENROLL SUCCESS!");
      Serial.println("[ENROLL] SUCCESS! ID Saved.");
      blinkLED(3);
    } else {
      statusChar.writeValue("Err: Save Failed");
    }
  } else {
    statusChar.writeValue("Err: Mismatch");
  }
}

void deleteFinger(int id) {
  statusChar.writeValue("Deleting...");
  delay(100);
  
  if (finger.deleteModel(id) == FINGERPRINT_OK) {
    statusChar.writeValue("Deleted ID " + String(id));
  } else {
    statusChar.writeValue("Err: Delete Failed");
  }
}

// ================================================================
// SECURITY & TRANSMISSION FUNCTIONS
// ================================================================

void executeUnlockSequenceBroadcast() {
  Serial.println(">>> STARTING ENCRYPTED BURST <<<");
  digitalWrite(LED_BUILTIN, HIGH);
  
  // 1. Stop Advertising to prepare new packet
  BLE.stopAdvertise(); 

  // 2. Increment and Save Rolling Code
  rollingCounter++;
  preferences.putUInt("counter", rollingCounter);
  
  // 3. Prepare Payload
  SecurePacket packet;
  packet.deviceID = 0xCAFEBABE; // Example ID
  packet.counter = rollingCounter;
  packet.command = 1; // UNLOCK Command
  for(int i=0; i<7; i++) packet.padding[i] = (uint8_t)random(0, 255);

  // 4. AES Encryption
  unsigned char inputBuffer[16];
  unsigned char outputBuffer[16];
  memcpy(inputBuffer, &packet, 16);
  
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, aes_key, 128);
  mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, inputBuffer, outputBuffer);
  mbedtls_aes_free(&aes);

  // 5. Set Advertising Data (Manufacturer Data)
  BLEAdvertisingData advData;
  advData.setManufacturerData(0xFFFF, outputBuffer, 16);
  BLE.setAdvertisingData(advData);

  // 6. Burst Transmission (600ms)
  BLE.advertise();
  delay(600); 
  BLE.stopAdvertise();
  
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println(">>> TX COMPLETE <<<");
  delay(1000); // Debounce delay
}

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

int getFingerprintID() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -1;
  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;
  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) return finger.fingerID;
  return -1;
}

void blinkLED(int times) {
  for(int i=0; i<times; i++) {
    digitalWrite(LED_BUILTIN, HIGH); delay(100);
    digitalWrite(LED_BUILTIN, LOW); delay(100);
  }
}
