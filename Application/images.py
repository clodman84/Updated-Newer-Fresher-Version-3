"""
This is responsible for serving images to the GUI.
This can be extended to receiving images from the DoPy server when it becomes a reality.
"""

import functools
import logging
from pathlib import Path
from collections import defaultdict
import shutil
from typing import Literal, Tuple

import numpy as np
import PIL.Image as PImage
import PIL.ImageOps as PImageOps

from .utils import ShittyMultiThreading

logger = logging.getLogger("Core.Images")


def next_available_path(path: Path) -> Path:
    if not path.exists():
        return path

    stem = path.stem
    suffix = path.suffix
    parent = path.parent

    i = 1
    while True:
        candidate = parent / f"{stem}_{i}{suffix}"
        if not candidate.exists():
            return candidate
        i += 1


class Image:
    """
    Dataclass that stores images that are served by the ImageManager. Dearpygui cannot display PImage.Image from Pillow
    directly, so this converts the image into a numpy array that dearpygui can display as a texture. The image is also padded
    with black borders if it's aspect ratio doesn't fit the ImageWindow.
    """

    _version = defaultdict(int)

    def __init__(
        self,
        path: Path,
        raw_image: PImage.Image,
        main_image_dimensions,
        thumbnail_dimensions,
    ) -> None:
        self.path = path
        self.raw_image = raw_image
        self.raw_image.putalpha(255)
        self.main_image_dimensions = main_image_dimensions
        self.thumbnail_dimensions = thumbnail_dimensions

    def save(self):
        new_dir = self.path.parents[1] / f"{self.path.parent.name}_original"
        new_dir.mkdir(parents=True, exist_ok=True)
        new_path = new_dir / self.path.name
        new_path = next_available_path(new_path)
        shutil.copy(self.path, new_path)
        with open(self.path, "wb") as file:
            self.raw_image = self.raw_image.convert("RGB")
            self.raw_image.save(file)

    @functools.cached_property
    def dpg_texture(self):
        dpg_texture = PImageOps.pad(
            self.raw_image, self.main_image_dimensions, color="#000000"
        )
        return np.frombuffer(dpg_texture.tobytes(), dtype=np.uint8) / 255.0

    @functools.cached_property
    def thumbnail(self):
        thumbnail = PImageOps.pad(
            self.raw_image, self.thumbnail_dimensions, color="#000000"
        )
        return np.frombuffer(thumbnail.tobytes(), dtype=np.uint8) / 255.0

    @functools.cached_property
    def dpg_raw(self):
        return np.frombuffer(self.raw_image.tobytes(), dtype=np.uint8) / 255.0

    @functools.cache
    def get_scaled_image(self, factor=0.15):
        return Image(
            self.path,
            PImageOps.scale(self.raw_image, factor),
            self.main_image_dimensions,
            self.thumbnail_dimensions,
        )

    @classmethod
    @functools.lru_cache(maxsize=40)
    def frompath(
        cls,
        path: Path,
        main_image_dimensions: Tuple[int, int],
        thumbnail_dimensions: Tuple[int, int],
        _version=None,
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
        except Exception:
            # I know that catching all exceptions is bad, but the range of errors is truly insane here
            logger.error(f"Something is seriously wrong with image: {str(path)}")
            raw_image = PImage.open("./dopylogofinal.png")

        logger.debug(f"Image made from path: {str(path)}")
        return Image(
            path,
            raw_image,
            main_image_dimensions,
            thumbnail_dimensions,
        )

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

    def load(self, index, force_reload=False):
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
        if force_reload:
            Image._version[image_path] += 1

        return Image.frompath(
            image_path,
            self.main_image_dimensions,
            self.thumbnail_dimensions,
            Image._version[image_path],
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
