import cv2
import os
import time

# Create dataset folder
if not os.path.exists('dataset'):
    os.makedirs('dataset')

# Initialize camera
cam = cv2.VideoCapture(0)
cam.set(3, 640)
cam.set(4, 480)

# Load detector
face_detector = cv2.CascadeClassifier(cv2.data.haarcascades + 'haarcascade_frontalface_default.xml')

face_id = input('\n Enter user ID (e.g., 1) and press Enter: ')
print("\n [INFO] Initializing. LOOK AT THE CAMERA!")
print(" [INFO] Taking 30 photos in 10-15 seconds...")

# Give time to pose
time.sleep(2)

count = 0
while True:
    ret, img = cam.read()
    if not ret:
        print("[ERR] Camera read error")
        break

    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    faces = face_detector.detectMultiScale(gray, 1.3, 5)

    for (x,y,w,h) in faces:

        count += 1

        # Save photo
        filename = f"dataset/User.{face_id}.{count}.jpg"
        cv2.imwrite(filename, gray[y:y+h,x:x+w])

        print(f"Photo {count}/30 saved.")

        #cv2.imshow(...)
        #cv2.waitKey(...)

    # Important pause to avoid taking 30 identical photos in 1 second
    time.sleep(0.3)

    if count >= 30:
        break

print("\n [INFO] Capture completed.")
cam.release()
# cv2.destroyAllWindows()