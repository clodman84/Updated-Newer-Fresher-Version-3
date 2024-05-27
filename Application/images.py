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

from .utils import ShittyMultiThreading

logger = logging.getLogger("Core.Images")


@dataclass
class Image:
    """This stores the images in a way that the GUI can represent, and since the classmethods are cached this greatly increases performance"""

    name: str
    raw_image: PImage.Image  # does raw_image even need to exist?
    dpg_texture: Tuple[int, int, int, np.ndarray]
    thumbnail: Tuple[int, int, int, np.ndarray]

    # TODO: the cache uses a lot of RAM there should be a way to control the maxsize at runtime
    @classmethod
    @functools.lru_cache(maxsize=40)
    def frompath(cls, path: Path):
        logger.debug(f"Making image from path: {str(path)}")
        """Makes an Image object from the specified Path"""
        raw_image = PImage.open(path)
        raw_image.putalpha(255)
        thumbnail = PImageOps.pad(raw_image, (245, 247), color="#000000")
        dpg_texture = PImageOps.pad(raw_image, (750, 500), color="#000000")

        # this frees up memory, PIL.Image.open() is lazy and does not load the image into memory till it needs to be
        raw_image = PImage.open(path)

        # dpg_texture-ifying
        channels = len(thumbnail.getbands())
        thumbnail = (
            245,
            247,
            channels,
            np.frombuffer(thumbnail.tobytes(), dtype=np.uint8) / 255.0,
        )
        dpg_texture = (
            750,
            500,
            channels,
            np.frombuffer(dpg_texture.tobytes(), dtype=np.uint8) / 255.0,
        )
        logger.debug(f"Image made from path: {str(path)}")
        return Image(path.name, raw_image, dpg_texture, thumbnail)

    @classmethod
    @functools.lru_cache(maxsize=40)
    def fromserver(cls, roll, id):
        """Makes an Image object by querying the DoPy server"""
        # TODO: creation of the scaled dpg texture and thumbnails should ideally take place on the server.
        raise NotImplemented


class ImageManager:
    """Does what the name suggests, creates Images. Regardless of wherever it is from, the interface stays the same"""

    def __init__(
        self,
        mode: Literal["online", "offline"],
        roll: str,
        path: Optional[Path] = None,
    ) -> None:
        if mode == "offline" and not Path:
            logging.error(
                "ImageManager initialised in offline mode, but no path was specified!"
            )
        self.mode = mode
        self.path = path
        logger.debug(path)
        self.roll = roll
        self.current_index = 0

    @functools.cached_property
    def images(self):
        if self.path:
            return sorted(list(self.path.iterdir()))
        else:
            # this may seem stupid right now, but this will replaced with a function
            # call to get image ids from the server, as the images will not be
            # renamed on the server.
            return list(range(1, 41))

    def load(self, index):
        logger.debug(f"Loading image {self.images[index]}")
        if index >= 40:
            logger.error("Attempted to get image number > 40, defaulted to 1")
            index = 0
        elif index < 0:
            logger.error("Attempted to get image number < 1, defaulted to 40")
            index = 39
        self.current_index = index
        if self.mode == "offline":
            image_path = self.images[index]
            return Image.frompath(image_path)
        return Image.fromserver(roll=self.roll, id=self.images[index])

    def load_in_background(self):
        ShittyMultiThreading(self.load, range(40)).start()

    def peek(self, index):
        """Loads without moving the current_index"""
        og = self.current_index
        img = self.load(index)
        self.current_index = og
        return img

    def next(self):
        curr = self.current_index
        if curr < 39:
            curr += 1
        else:
            curr = 0
        return self.peek(curr)

    def previous(self):
        curr = self.current_index
        if curr > 0:
            curr -= 1
        else:
            curr = 39
        return self.peek(curr)
