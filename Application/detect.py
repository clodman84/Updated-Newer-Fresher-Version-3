import cv2
import numpy as np
from pathlib import Path
import logging
import functools
from PIL import Image, ImageOps

logger = logging.getLogger("Core.Face_Detector")

detector = cv2.FaceDetectorYN.create(
        "./Data/face_detection_yunet_2023mar.onnx",
        "",
        (320, 320),
        0.8,
        0.3,
        5000
    )

@functools.lru_cache(maxsize=360)
def detect(path: Path):
    # This isn't particularly good though
    # (it's actually quite bad)
    try:
        img = cv2.imread(str(path))
        imgHeight, imgWidth = img.shape[:2]
        detector.setInputSize((imgWidth, imgHeight))
        faces = detector.detect(img)
        logger.info(faces)
        return faces
    except Exception:
        return "Something Went Wrong!"

def visualise(path: Path, faces, dimensions, thickness=5):
    image = cv2.imread(str(path))
    if faces[1] is not None:
        for idx, face in enumerate(faces[1]):
            logger.debug('Face {}, top-left coordinates: ({:.0f}, {:.0f}), box width: {:.0f}, box height {:.0f}, score: {:.2f}'.format(idx, face[0], face[1], face[2], face[3], face[-1]))
            coords = face[:-1].astype(np.int32)
            cv2.rectangle(image, (coords[0], coords[1]), (coords[0]+coords[2], coords[1]+coords[3]), (0, 255, 0), thickness)
            cv2.putText(image, f"{idx + 1}", (coords[0], coords[1] - 15), cv2.FONT_HERSHEY_SIMPLEX, 3, (0, 255, 0), 4)
    image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
    im_pil = Image.fromarray(image)
    im_pil.putalpha(255)
    im_pil = ImageOps.pad(im_pil, dimensions, color="#000000")
    return np.frombuffer(im_pil.tobytes(), dtype=np.uint8) / 255.0


