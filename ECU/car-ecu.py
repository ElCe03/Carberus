import asyncio
import cv2
import serial_asyncio
import time
import os
from bleak import BleakScanner
from Crypto.Cipher import AES

# =================================================================
#  CARBERUS ECU - MASTER CONTROL UNIT
#  Hardware: Raspberry Pi 4/5 + Official Camera + Bluetooth
# =================================================================

# --- CONFIGURATION ---
SERIAL_BAUD_RATE = 9600
TRAINER_FILE = 'trainer.yml'
CASCADE_PATH = cv2.data.haarcascades + 'haarcascade_frontalface_default.xml'

# --- AES KEY (Must match Smart Key Firmware) ---
AES_KEY = bytes([0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F])

# Authorized Users Label
NAMES = ['None', 'User1', 'User2', 'User3']

# --- GLOBAL STATE ---
arduino_transport = None
last_unlock_time = 0
unlock_request_ble = False
last_ble_counter = 0

# =================================================================
#  SERIAL PROTOCOL (Communication with BCM/Arduino)
# =================================================================
class ArduinoProtocol(asyncio.Protocol):
    def connection_made(self, transport):
        self.transport = transport
        print("[HW] BCM Connected (Serial).")
        global arduino_transport
        arduino_transport = transport

    def data_received(self, data):
        try:
            message = data.decode().strip()
            # Handle status messages from BCM
            if message == "ACK:UNLOCKED":
                print("[BCM] Door Unlocked.")
            elif message == "ACK:LOCKED":
                print("[BCM] Door Locked.")
            elif message == "STATUS:TIMEOUT":
                print("[SYS] Auto-relock triggered.")
        except:
            pass

    def connection_lost(self, exc):
        print("[HW] BCM Disconnected.")

# =================================================================
#  CORE LOGIC
# =================================================================
def trigger_unlock(source="Unknown"):
    global arduino_transport, last_unlock_time

    # 5-second cooldown to prevent command flooding
    if time.time() - last_unlock_time > 5:
        print(f"\n>>> ACCESS GRANTED via {source} <<<")

        if arduino_transport:
            arduino_transport.write(b"CMD_UNLOCK\n")
            last_unlock_time = time.time()
            return True
        else:
            print("[ERR] BCM not connected. Cannot unlock.")
    return False

# BLE Advertising Callback
def ble_detection_callback(device, advertisement_data):
    global unlock_request_ble, last_ble_counter

    # Filter for our Manufacturer ID (0xFFFF is used for demo)
    if 0xFFFF in advertisement_data.manufacturer_data:
        encrypted_data = advertisement_data.manufacturer_data[0xFFFF]

        if len(encrypted_data) == 16:
            try:
                # 1. Decrypt Payload
                cipher = AES.new(AES_KEY, AES.MODE_ECB)
                decrypted = cipher.decrypt(encrypted_data)

                # 2. Extract Data
                # Structure: [ID:4] [Counter:4] [Cmd:1] [Pad:7]
                packet_counter = int.from_bytes(decrypted[4:8], byteorder='little')
                command = decrypted[8]

                # 3. Security Validation (Anti-Replay)
                if command == 1:
                    if packet_counter > last_ble_counter:
                        # Valid Fresh Packet
                        print(f"[BLE] Verified Signal Received (Counter: {packet_counter})")
                        last_ble_counter = packet_counter
                        unlock_request_ble = True
                    else:
                        # Replay Attack or Old Packet
                        # We log this for security audit, but do NOT trigger unlock
                        print(f"[SEC] Replay Attack Blocked (Counter {packet_counter} <= {last_ble_counter})")

            except Exception:
                # Silent fail on bad packets
                pass

# =================================================================
#  MAIN LOOP
# =================================================================
async def main_loop():
    global unlock_request_ble

    print("\n" + "="*40)
    print(" CARBERUS ECU v1.0 - RUNNING")
    print(" Security Modules: BLE + FaceID")
    print("="*40 + "\n")

    # 1. Establish Serial Connection
    loop = asyncio.get_running_loop()
    try:
        # Auto-detect standard ports
        if os.path.exists('/dev/ttyACM0'):
            await serial_asyncio.create_serial_connection(loop, ArduinoProtocol, '/dev/ttyACM0', baudrate=SERIAL_BAUD_RATE)
        elif os.path.exists('/dev/ttyUSB0'):
            await serial_asyncio.create_serial_connection(loop, ArduinoProtocol, '/dev/ttyUSB0', baudrate=SERIAL_BAUD_RATE)
        else:
            print("[WARN] No Serial Device found. Running in Logic-Only mode.")
    except Exception as e:
        print(f"[ERR] Serial Init Failed: {e}")

    # 2. Initialize Computer Vision
    face_rec_enabled = False
    if os.path.exists(TRAINER_FILE):
        recognizer = cv2.face.LBPHFaceRecognizer_create()
        recognizer.read(TRAINER_FILE)
        face_cascade = cv2.CascadeClassifier(CASCADE_PATH)
        face_rec_enabled = True
        print("[VIS] Face Recognition Model Loaded.")
    else:
        print("[WARN] Model file not found. FaceID Disabled.")

    # 3. Start BLE Scanner (Background)
    scanner = BleakScanner(detection_callback=ble_detection_callback)
    await scanner.start()
    print("[BLE] Scanner Active (Listening for Encrypted Broadcasts...)")

    # 4. Start Camera
    cam = cv2.VideoCapture(0)
    cam.set(3, 320) # Width
    cam.set(4, 240) # Height

    # --- EVENT LOOP ---
    try:
        while True:
            # CHECK 1: Bluetooth Request
            if unlock_request_ble:
                trigger_unlock("SMART KEY")
                unlock_request_ble = False

            # CHECK 2: Face Recognition
            ret, img = cam.read()
            if ret and face_rec_enabled:
                gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
                faces = face_cascade.detectMultiScale(gray, 1.2, 5)

                for(x,y,w,h) in faces:
                    try:
                        id, confidence = recognizer.predict(gray[y:y+h,x:x+w])
                        # Confidence < 60 means good match
                        if confidence < 60:
                            name = NAMES[id] if id < len(NAMES) else f"ID_{id}"
                            trigger_unlock(f"FACE ID ({name})")
                    except:
                        pass

            # Yield control to async tasks (BLE)
            await asyncio.sleep(0.05)

    except KeyboardInterrupt:
        print("\n[SYS] Shutdown initiated...")
    finally:
        await scanner.stop()
        cam.release()
        print("[SYS] Resources released. Goodbye.")

if __name__ == "__main__":
    try:
        asyncio.run(main_loop())
    except KeyboardInterrupt:
        pass