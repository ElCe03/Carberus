# Carberus
An open-source, two-factor automotive security system (Fingerprint + FaceID). Features a BLE Smart Key with AES-128 encryption, Anti-Replay protection (Rolling Code), and a native Android management app.
## 🚀 Key Features

### 🔒 Security & Encryption
* **AES-128 Encryption:** Every packet transmitted via Bluetooth Low Energy (BLE) is end-to-end encrypted.
* **Anti-Replay (Rolling Code):** A proprietary algorithm that invalidates used packets, rendering "Sniffing & Replay" attacks useless.
* **Dual-Factor Authentication (2FA):** Access requires **Physical Biometrics** (Fingerprint on the key) + **Visual Verification** (FaceID on the car).

### ⚡ Hardware Efficiency
* **Burst Mode Transmission:** The Smart Key transmits for only **600ms** to maximize battery life.
* **Edge Processing:** Fingerprint recognition is performed locally on the key (Arduino Nano ESP32).
* **Asyncio Core:** The ECU (Raspberry Pi) manages video streams and radio decryption simultaneously with zero latency.

### 📱 Mobile Management
* **Android Native App:** Built in Kotlin, it acts as an Admin Console to manage the user fleet (Enroll/Delete fingerprints) by connecting directly to the key via BLE.

---

## 🏗️ System Architecture

The system consists of three interconnected nodes:

1.  **🔑 Smart Key (Tx):**
    * *Hardware:* Arduino Nano ESP32 + AS608 Fingerprint Sensor.
    * *Function:* Scans the fingerprint, encrypts the payload (ID + Counter + Command), and broadcasts it via BLE.
2.  **🧠 Master ECU (Rx/Logic):**
    * *Hardware:* Raspberry Pi 4/5 + Pi Camera.
    * *Software:* Python (Bleak, OpenCV, PyCryptodome).
    * *Function:* Captures BLE packets, decrypts them, validates the Rolling Code, performs facial recognition, and commands the actuator.
3.  **🚪 BCM - Body Control Module (Actuator):**
    * *Hardware:* Arduino Uno + Servo Motor.
    * *Function:* Physically executes the locking/unlocking upon serial command from the ECU. Includes "Auto-Relock" safety logic.

![System Diagram](path/to/your/architecture_diagram.jpg) ---
