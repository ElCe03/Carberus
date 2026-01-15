import cv2
import numpy as np
import os
from PIL import Image

path = 'dataset'
recognizer = cv2.face.LBPHFaceRecognizer_create()
detector = cv2.CascadeClassifier(cv2.data.haarcascades + 'haarcascade_frontalface_default.xml')

def getImagesAndLabels(path):
    imagePaths = [os.path.join(path,f) for f in os.listdir(path)]
    faceSamples=[]
    ids = []

    for imagePath in imagePaths:
        # Ignore files that are not images
        if not imagePath.endswith("jpg"): continue

        # Load image in grayscale
        PIL_img = Image.open(imagePath).convert('L')
        img_numpy = np.array(PIL_img,'uint8')

        # Extract ID from filename
        id = int(os.path.split(imagePath)[-1].split(".")[1])

        faces = detector.detectMultiScale(img_numpy)
        for (x,y,w,h) in faces:
            faceSamples.append(img_numpy[y:y+h,x:x+w])
            ids.append(id)

    return faceSamples,ids

print ("\n [INFO] Facial training in progress. Please wait a few seconds...")
faces, ids = getImagesAndLabels(path)

if len(ids) > 0:
    recognizer.train(faces, np.array(ids))
    # Save the model
    recognizer.write('trainer.yml')
    print(f"\n [SUCCESS] {len(np.unique(ids))} users trained. File 'trainer.yml' created.")
else:
    print("\n [ERROR] No faces found in dataset.")