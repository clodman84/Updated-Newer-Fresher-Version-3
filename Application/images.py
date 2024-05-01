"""
This is responsible for serving images to the GUI.
This can be extended to receiving images from the DoPy server when it becomes a reality.
"""

import functools
import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Tuple

import numpy as np
import PIL.Image as PImage
import PIL.ImageOps as PImageOps

logger = logging.getLogger("Core.Images")


@dataclass
class Image:
    raw_image: PImage.Image
    dpg_texture: Tuple[int, int, int, np.ndarray]
    thumbnail: Tuple[int, int, int, np.ndarray]

    @classmethod
    @functools.lru_cache(maxsize=40)
    def frompath(cls, path: Path):
        """Makes an Image object from the specified Path"""
        raw_image = PImage.open(path)
        raw_image.putalpha(255)
        thumbnail = PImageOps.pad(raw_image, (128, 128), color="#000000")
        dpg_texture = PImageOps.pad(raw_image, (750, 500), color="#000000")

        # dpg_texture-ifying
        channels = len(thumbnail.getbands())
        logger.debug(f"Channels: {channels}")

        thumbnail = (
            128,
            128,
            channels,
            np.frombuffer(thumbnail.tobytes(), dtype=np.uint8) / 255.0,
        )
        dpg_texture = (
            750,
            500,
            channels,
            np.frombuffer(dpg_texture.tobytes(), dtype=np.uint8) / 255.0,
        )
        return Image(raw_image, dpg_texture, thumbnail)

    @classmethod
    @functools.lru_cache(maxsize=40)
    def fromserver(cls, cam, roll, id):
        """Makes an Image object by querying the DoPy server"""
        raise NotImplemented
