import logging

import dearpygui.dearpygui as dpg

from Application import ImageManager, detect, visualise
from Application.utils import ShittyMultiThreading

from .bill import BillingWindow

logger = logging.getLogger("GUI.Image")


class ImageWindow:
    """imej"""

    def __init__(
        self,
        roll: str,
        detect_faces: bool,
        image_manager: ImageManager,
        main_image_dimensions,
        thumnail_dimensions,
        window_dimensions,
    ):
        # List of things the image window knows about:
        # 1. The roll that is currently being billed
        # 2. The images in the roll
        # 3. A BilledWindow

        # What does the ImageWindow do?
        # 1. Creates and manages the BilledWindow
        # 2. Lets us open our image of choice
        # 3. Has a preview for the next and previous image

        self.current_image: int = 0
        self.detect_faces = detect_faces
        self.roll = roll

        self.image_manager = image_manager

        self.main_image_dimensions = main_image_dimensions
        self.thumnail_dimensions = thumnail_dimensions
        self.window_dimensions = window_dimensions

        with dpg.window(
            label=self.roll,
            width=self.window_dimensions[0],
            height=self.window_dimensions[1],
        ):
            with dpg.group(horizontal=True) as self.parent:
                self.billing_window = BillingWindow(
                    roll=self.roll, source=self.image_manager.images, parent=self.parent
                )
                self.setup()
        self.image_manager.load_in_background()

    def setup(self):
        logger.debug("Setting Up Image Window")
        with dpg.child_window():
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
                dpg.add_button(
                    label="Count Faces",
                    callback=self.count_faces,
                    show=self.detect_faces,
                )
                dpg.add_text("", tag=f"{self.parent}_face_count")

            with dpg.group(horizontal=False):
                dpg.add_image(f"{self.parent}_Main Image")
                # with dpg.group(horizontal=True):
                #     dpg.add_image(f"{self.parent}_Previous Image")
                #     dpg.add_image(f"{self.parent}_Next Image")
            dpg.delete_item(indicator)

            # ultra shitty way to detect all the faces in the background (very bad)
            if self.detect_faces:
                ShittyMultiThreading(
                    detect, self.image_manager.images, num_threads=1
                ).start()

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
        faces = detect(path)
        if faces is not str and faces[1] is not None:
            n_faces = len(faces[1])
            dpg.set_value(
                f"{self.parent}_face_count",
                f"{n_faces} face{'' if n_faces==1 else 's'} detected",
            )
        dimensions = self.image_manager.load(self.current_image).dpg_texture[:2]
        updated_image = visualise(path, faces, dimensions)
        dpg.set_value(f"{self.parent}_Main Image", updated_image)
        dpg.delete_item(indicator)
