"""
This is responsible for serving images to the GUI.
This can be extended to receiving images from the DoPy server when it becomes a reality.
"""

import functools
import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Literal, Optional, Tuple

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


class ImageManager:
    def __init__(
        self,
        mode: Literal["online", "offline"],
        cam: str,
        roll: str,
        path: Optional[Path] = None,
    ) -> None:
        if mode == "offline" and not Path:
            logging.error(
                "ImageManager initialised in offline mode, but no path was specified!"
            )
        self.mode = mode
        self.path = path
        self.cam = cam
        self.roll = roll
        self.current_index = 0

    def load(self, index):
        logger.debug(f"Loading image {index}")
        if index >= 40:
            logger.error("Attempted to get image number > 40, defaulted to 40")
            index = 39
        elif index < 0:
            logger.error("Attempted to get image number < 1, defaulted to 40")
            index = 0

        self.current_index = index
        if self.mode == "offline":
            image_path = self.images[index]
            return Image.frompath(image_path)
        return Image.fromserver(cam=self.cam, roll=self.roll, id=self.images[index])

    @functools.cached_property
    def images(self):
        if self.path:
            return sorted(list(self.path.iterdir()), key=lambda x: x.name)
        else:
            # this may seem stupid right now, but this will replaced with a function call to get image ids from the server, as the images will not be renamed on the server.
            return list(range(1, 41))

    def next(self):
        if self.current_index < 40:
            self.load(self.current_index + 1)
            self.current_index += 1

    def previous(self):
        if self.current_index > 1:
            self.load(self.current_index - 1)
            self.current_index -= 1
