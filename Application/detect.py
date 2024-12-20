import cv2
from pathlib import Path
import logging
import functools

logger = logging.getLogger("Core.Face_Detector")

detector = cv2.FaceDetectorYN.create(
        "./Data/face_detection_yunet_2023mar.onnx",
        "",
        (320, 320),
        0.9,
        0.3,
        5000
    )

@functools.lru_cache(maxsize=360)
def detect(path: Path):
    # This isn't particularly good though
    img = cv2.imread(str(path))
    imgHeight, imgWidth = img.shape[:2]
    detector.setInputSize((imgWidth, imgHeight))
    faces = detector.detect(img)
    logger.info(faces[1])
    if faces[1] is not None:
        return len(faces[1])
    return 0
