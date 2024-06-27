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
    """
    Dataclass that stores images that are served by the ImageManager. Dearpygui cannot display PImage.Image from Pillow
    directly, so this converts the image into a numpy array that dearpygui can display as a texture. The image is also padded
    with black borders if it's aspect ratio doesn't fit the ImageWindow.

    Attributes:
        name: The name of the image file
        raw_image: PImage.Image object
        dpg_texture: A scaled image that is shown in the bigger display, stored in a form that dearpygui accepts
        thumbnail: A scaled thumbnail that is shown in the preview displays, stored in a form that dearpygui accepts
    """

    name: str
    raw_image: PImage.Image  # does raw_image even need to exist?
    dpg_texture: Tuple[int, int, int, np.ndarray]
    thumbnail: Tuple[int, int, int, np.ndarray]

    # TODO: the cache uses a lot of RAM there should be a way to control the maxsize at runtime
    @classmethod
    @functools.lru_cache(maxsize=40)
    def frompath(cls, path: Path):
        """
        Creates an Image object from the path of the image.

        Args:
            path (Path): Path to the image

        Returns:
            Image
        """
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
        """
        Implement this function when the server is up and running. This doesn't work right now.

        Args:
            roll (str):
            id (str):
        """
        """Makes an Image object by querying the DoPy server"""
        # TODO: creation of the scaled dpg texture and thumbnails should ideally take place on the server.
        raise NotImplemented


class ImageManager:
    """
    Does what the name suggests, creates Images. Regardless of wherever it is from, the interface stays the same

    Attributes:
        mode: Whether the images are being created from a local file or via the server
        path: Path to the folder containing all the images (This has to be there if it is loading images in offline mode)
        roll: The roll number
        current_index: The index of the image that is currently being displayed
    """

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
        """
        A sorted list of all the images

        Returns: list
        """
        if self.path:
            return sorted(list(self.path.iterdir()))
        else:
            # this may seem stupid right now, but this will replaced with a function
            # call to get image ids from the server, as the images will not be
            # renamed on the server.
            return list(range(1, 41))

    def load(self, index):
        """
        Returns an Image object, given an index

        Args:
            index (int): The index of the roll that needs to be loaded

        Returns:

        """
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
        """
        Loads all the images in the background using ShittMultiThreading from utils.py
        This works because the images are cached.
        """
        ShittyMultiThreading(self.load, range(40)).start()

    def peek(self, index):
        """
        Returns an image object without moving the current_index

        Args:
            index (int): The index of the image we want to view

        Returns: Image
        """
        og = self.current_index
        img = self.load(index)
        self.current_index = og
        return img

    def next(self):
        """
        Helper function to peek the next image

        Returns:
           Image
        """
        curr = self.current_index
        if curr < 39:
            curr += 1
        else:
            curr = 0
        return self.peek(curr)

    def previous(self):
        """
        Helper function to peek the previous image

        Returns:
           Image
        """
        curr = self.current_index
        if curr > 0:
            curr -= 1
        else:
            curr = 39
        return self.peek(curr)
