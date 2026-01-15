/*
  Project: CARBERUS - Body Control Module (BCM)
  Hardware: Arduino Uno + Servo + Buzzer
  Logic: Open Loop 
*/

#include <Servo.h>

// --- PIN CONFIGURATION ---
const int PIN_SERVO = 9;
const int PIN_BUZZER = 5;

Servo myServo;

// --- SYSTEM STATES ---
enum State { LOCKED, UNLOCKED };
State currentState = LOCKED;

unsigned long unlockTime = 0;
const int AUTO_RELOCK_DELAY = 10000; // 10 Seconds auto-relock timer

void setup() {
  Serial.begin(9600);
  pinMode(PIN_BUZZER, OUTPUT);
  
  myServo.attach(PIN_SERVO);
  lockDoor(); // Ensure door is locked on startup
  
  Serial.println("BCM READY (NO SENSOR MODE)");
}

void loop() {
  // 1. Read Commands from Raspberry Pi
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

  // 2. Logic: Auto-Relock Timer
  // Since we don't have a sensor, we just rely on the timer.
  // If unlocked for more than X seconds -> Lock it back.
  if (currentState == UNLOCKED && (millis() - unlockTime > AUTO_RELOCK_DELAY)) {
    Serial.println("STATUS:TIMEOUT");
    lockDoor(); 
  }
}

// --- ACTIONS ---

void unlockDoor() {
  // Only act if currently locked to avoid servo jitter
  if (currentState == LOCKED) {
    myServo.write(90); // Unlock Position (Adjust angle if needed)
    currentState = UNLOCKED;
    unlockTime = millis(); // Reset timer
    
    // Audio Feedback (2 High Beeps)
    tone(PIN_BUZZER, 2000, 100); delay(150);
    tone(PIN_BUZZER, 2000, 100);
    
    Serial.println("ACK:UNLOCKED");
  }
}

void lockDoor() {
  if (currentState != LOCKED) {
    myServo.write(0); // Lock Position (Adjust angle if needed)
    currentState = LOCKED;
    
    // Audio Feedback (1 Low Long Beep)
    tone(PIN_BUZZER, 1000, 500);
    
    Serial.println("ACK:LOCKED");
  }
}