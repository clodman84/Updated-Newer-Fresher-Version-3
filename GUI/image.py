import logging
from pathlib import Path

import dearpygui.dearpygui as dpg
from screeninfo import get_monitors

from Application import ImageManager, detect
from Application.utils import ShittyMultiThreading

from .bill import BillingWindow

logger = logging.getLogger("GUI.Image")


class ImageWindow:
    """imej"""

    def __init__(self, path: Path):
        # List of things the image window knows about:
        # 1. The roll that is currently being billed
        # 2. The images in the roll
        # 3. A BilledWindow

        # What does the ImageWindow do?
        # 1. Creates and manages the BilledWindow
        # 2. Lets us open our image of choice
        # 3. Has a preview for the next and previous image

        self.current_image: int = 0
        self.main_image_ratios = (0.55, 0.65)
        self.thumnail_ratios = (0.18, 0.32)
        self.window_ratios = (0.76, 0.79)
        self.path = path

        monitors = get_monitors()
        for monitor in monitors:
            if monitor.is_primary:
                self.main_image_dimensions = tuple(
                    int(j * i)
                    for i, j in zip(
                        self.main_image_ratios, (monitor.width, monitor.height)
                    )
                )
                self.thumnail_dimensions = tuple(
                    int(j * i)
                    for i, j in zip(
                        self.thumnail_ratios, (monitor.width, monitor.height)
                    )
                )
                self.window_dimensions = tuple(
                    int(j * i)
                    for i, j in zip(self.window_ratios, (monitor.width, monitor.height))
                )

        self.image_manager = ImageManager(
            mode="offline",
            roll=path.name,
            path=path,
            main_image_dimensions=self.main_image_dimensions,
            thumbnail_dimensions=self.thumnail_dimensions,
        )
        self.billing_window = BillingWindow(roll=path.name, path=path, num_images=self.image_manager.end_index)
        self.setup()
        self.image_manager.load_in_background()

    def setup(self):
        self.parent = dpg.add_window(
            label=self.path.name,
            width=self.window_dimensions[0],
            height=self.window_dimensions[1],
            on_close=self.billing_window.close,
        )
        with dpg.child_window(parent=self.parent):
            indicator = dpg.add_loading_indicator()

            # this is an abomination, but it makes the window load 2 seconds faster
            ShittyMultiThreading(
                self.image_manager.load, (0, 1, self.image_manager.end_index - 1)
            ).start()
            image = self.image_manager.load(0)
            logger.debug(image.dpg_texture[3].shape)

            with dpg.texture_registry():
                # TODO: The next and previous image viewer could be changed into a scrollable selector
                # with all the images in them
                dpg.add_dynamic_texture(
                    *self.main_image_dimensions,
                    default_value=image.dpg_texture[3],
                    tag=f"{self.parent}_Main Image",
                )
                next = self.image_manager.next()
                previous = self.image_manager.previous()
                dpg.add_dynamic_texture(
                    *self.thumnail_dimensions,
                    default_value=next.thumbnail[3],
                    tag=f"{self.parent}_Next Image",
                )
                dpg.add_dynamic_texture(
                    *self.thumnail_dimensions,
                    default_value=previous.thumbnail[3],
                    tag=f"{self.parent}_Previous Image",
                )

            with dpg.group(horizontal=True) as self.ribbon:
                dpg.add_button(label="Next", callback=self.next)
                dpg.add_button(label="Previous", callback=self.previous)
                dpg.add_slider_int(
                    default_value=1,
                    min_value=1,
                    max_value=self.image_manager.end_index,
                    callback=lambda _, a, u: self.open(a - 1),
                    tag=f"{self.parent}_Image Slider",
                )
                dpg.add_button(label="Count Faces", callback=self.count_faces)
                dpg.add_text("", tag=f"{self.parent}_face_count")

            with dpg.group(horizontal=True):
                with dpg.group():
                    dpg.add_image(f"{self.parent}_Previous Image")
                    dpg.add_image(f"{self.parent}_Next Image")
                dpg.add_image(f"{self.parent}_Main Image")
            dpg.delete_item(indicator)

    def open(self, index: int):
        self.current_image = index

        image = self.image_manager.load(index)
        previous = self.image_manager.previous()
        next = self.image_manager.next()

        dpg.set_value(f"{self.parent}_Main Image", image.dpg_texture[3])
        dpg.set_value(f"{self.parent}_Next Image", next.thumbnail[3])
        dpg.set_value(f"{self.parent}_Previous Image", previous.thumbnail[3])
        dpg.set_value(f"{self.parent}_Image Slider", self.current_image + 1)
        dpg.set_value(f"{self.parent}_face_count", "")

        self.billing_window.load(index)

    def next(self):
        if self.current_image < self.image_manager.end_index - 1:
            self.open(self.current_image + 1)
        else:
            self.open(0)

    def previous(self):
        if self.current_image > 0:
            self.open(self.current_image - 1)
        else:
            self.open(self.image_manager.end_index - 1)

    def count_faces(self):
        path = self.image_manager.images[self.current_image]
        indicator = dpg.add_loading_indicator(parent=self.ribbon)
        count = detect(path)
        dpg.set_value(f"{self.parent}_face_count", f"{count} face{'' if count == 1 else 's'} detected!")
        dpg.delete_item(indicator)
