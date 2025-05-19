"""
This is responsible for serving images to the GUI.
This can be extended to receiving images from the DoPy server when it becomes a reality.
"""

import functools
import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Literal, Tuple

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
    raw_image: PImage.Image
    dpg_texture: Tuple[int, int, int, np.ndarray]
    thumbnail: Tuple[int, int, int, np.ndarray]

    # TODO: the cache uses a lot of RAM there should be a way to control the maxsize at runtime
    @classmethod
    @functools.lru_cache(maxsize=40)
    def frompath(
        cls,
        path: Path,
        main_image_dimensions: Tuple[int, int],
        thumbnail_dimensions: Tuple[int, int],
    ):
        """
        Creates an Image object from the path of the image.

        Args:
            path (Path): Path to the image

        Returns:
            Image
        """
        logger.debug(f"Making image from path: {str(path)}")
        """Makes an Image object from the specified Path"""
        try:
            raw_image = PImage.open(path)
            raw_image.putalpha(255)
            thumbnail = PImageOps.pad(raw_image, thumbnail_dimensions, color="#000000")
            dpg_texture = PImageOps.pad(
                raw_image, main_image_dimensions, color="#000000"
            )
        except Exception:
            # I know that catching all exceptions is bad, but the range of errors is truly insane here
            logger.error(f"Something is seriously wrong with image: {str(path)}")
            raw_image = PImage.open("./dopylogofinal.png")
            raw_image.putalpha(255)
            thumbnail = PImageOps.pad(raw_image, thumbnail_dimensions, color="#000000")
            dpg_texture = PImageOps.pad(
                raw_image, main_image_dimensions, color="#000000"
            )

        # this frees up memory, PIL.Image.open() is lazy and does not load the image into memory till it needs to be
        try:
            raw_image = PImage.open(path)
        except PImage.UnidentifiedImageError:
            pass

        # dpg_texture-ifying
        channels = len(thumbnail.getbands())
        thumbnail = (
            *thumbnail_dimensions,
            channels,
            np.frombuffer(thumbnail.tobytes(), dtype=np.uint8) / 255.0,
        )
        dpg_texture = (
            *main_image_dimensions,
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
    """

    def __init__(
        self,
        mode: Literal["online", "offline"],
        main_image_dimensions,
        thumbnail_dimensions,
    ) -> None:
        if mode == "offline" and not Path:
            logging.error(
                "ImageManager initialised in offline mode, but no path was specified!"
            )
        self.mode = mode
        self.current_index = 0
        self.main_image_dimensions = main_image_dimensions
        self.thumbnail_dimensions = thumbnail_dimensions
        self.images: list[Path] = []

    @classmethod
    def from_path(cls, path: Path, main_image_dimensions, thumbnail_dimensions):
        image_manager = cls("offline", main_image_dimensions, thumbnail_dimensions)
        image_manager.images = sorted(list(path.iterdir()))
        return image_manager

    @classmethod
    def from_file_list(
        cls, files: list[Path], main_image_dimensions, thumbnail_dimensions
    ):
        image_manager = cls("offline", main_image_dimensions, thumbnail_dimensions)
        image_manager.images = files
        return image_manager

    @classmethod
    def from_server(cls, url):
        return NotImplementedError

    @functools.cached_property
    def end_index(self):
        return len(self.images)

    def load(self, index):
        """
        Returns an Image object, given an index

        Args:
            index (int): The index of the roll that needs to be loaded

        Returns:

        """
        logger.debug(f"Loading image {self.images[index]}")
        if index >= self.end_index:
            print(self.end_index)
            logger.error(
                f"Attempted to get image number > {self.end_index}, defaulted to 1"
            )
            index = 0
        elif index < 0:
            logger.error(
                f"Attempted to get image number < 1, defaulted to {self.end_index}"
            )
            index = self.end_index - 1
        self.current_index = index
        image_path = self.images[index]
        return Image.frompath(
            image_path, self.main_image_dimensions, self.thumbnail_dimensions
        )

    def load_in_background(self):
        """
        Loads all the images in the background using ShittMultiThreading from utils.py
        This works because the images are cached.
        """
        ShittyMultiThreading(self.load, range(self.end_index)).start()

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
        if curr < self.end_index - 1:
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
            curr = self.end_index - 1
        return self.peek(curr)
