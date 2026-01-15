/*
  Project: CARBERUS - Body Control Module (BCM)
  Hardware: Arduino Uno + Servo + Buzzer + LED
  Logic: Open Loop with Manual Tone Generation
*/

#include <Servo.h>

// --- PIN CONFIGURATION ---
const int PIN_SERVO = 9;
const int PIN_BUZZER = 5;
const int STATUS_LED = 13;

Servo myServo;

// --- SYSTEM STATES ---
enum State { LOCKED, UNLOCKED };
State currentState = LOCKED;

unsigned long unlockTime = 0;
const int AUTO_RELOCK_DELAY = 10000; // 10 Seconds auto-relock

void setup() {
  Serial.begin(9600);
  
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);
  
  // Reset output
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(STATUS_LED, LOW);
  
  myServo.attach(PIN_SERVO);
  
  // Initial safety position
  myServo.write(0); 
  currentState = LOCKED;
  
  // Visual startup feedback (3 blinks)
  for(int i=0; i<3; i++) {
    digitalWrite(STATUS_LED, HIGH); delay(100);
    digitalWrite(STATUS_LED, LOW); delay(100);
  }
  
  Serial.println("BCM READY (NO SENSOR MODE)");
}

void loop() {
  // 1. Read Commands
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd == "CMD_UNLOCK") {
      unlockDoor();
    }
    else if (cmd == "CMD_LOCK") {
      lockDoor();
    }
  }

  // 2. Auto-Relock Logic
  if (currentState == UNLOCKED && (millis() - unlockTime > AUTO_RELOCK_DELAY)) {
    Serial.println("STATUS:TIMEOUT");
    lockDoor(); 
  }
}

// --- MANUAL SOUND FUNCTION (SERVO FIX) ---
// Manually generates a square wave to avoid conflicts with the Servo library
void manualBeep(int frequency, int duration_ms) {
  long delayAmount = (long)(1000000 / frequency) / 2;
  long numCycles = frequency * duration_ms / 1000;
  
  for (long i = 0; i < numCycles; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delayMicroseconds(delayAmount);
    digitalWrite(PIN_BUZZER, LOW);
    delayMicroseconds(delayAmount);
  }
}

// --- ACTIONS ---

void unlockDoor() {
  if (currentState == LOCKED) {
    myServo.write(90); // Unlock Position
    currentState = UNLOCKED;
    unlockTime = millis(); // Reset timer
    
    // Feedback: DOUBLE HIGH BEEP + LED
    // 1st Beep
    digitalWrite(STATUS_LED, HIGH);
    manualBeep(2000, 100); // 2000Hz for 100ms
    digitalWrite(STATUS_LED, LOW);
    
    delay(100); // Silent pause
    
    // 2nd Beep
    digitalWrite(STATUS_LED, HIGH);
    manualBeep(2000, 100); 
    digitalWrite(STATUS_LED, LOW);
    
    Serial.println("ACK:UNLOCKED");
  }
}

void lockDoor() {
  if (currentState != LOCKED) {
    myServo.write(0); // Lock Position
    currentState = LOCKED;
    
    // Feedback: SINGLE LOW BEEP + LED
    digitalWrite(STATUS_LED, HIGH);
    manualBeep(800, 300); // 800Hz for 300ms
    digitalWrite(STATUS_LED, LOW);
    
    Serial.println("ACK:LOCKED");
  }
}
