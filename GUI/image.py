import logging
from pathlib import Path

import dearpygui.dearpygui as dpg
from screeninfo import get_monitors

from Application import ImageManager
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
        self.thumnail_ratios = (0.18, 0.32)  # i luv speling
        self.window_ratios = (0.76, 0.79)
        self.path = path

        monitors = get_monitors()  # Gets the information regarding your monitors

        for monitor in monitors:
            # Simply adjusts image size based on monitor dimensions
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
                    # Similar
                )

                self.window_dimensions = tuple(
                    int(j * i)
                    for i, j in zip(self.window_ratios, (monitor.width, monitor.height))
                    # Similar
                )

        self.image_manager = ImageManager(
            # Initialises the Image Manager (found in Application/images.py)
            mode="offline",
            roll=path.name,
            path=path,
            main_image_dimensions=self.main_image_dimensions,
            thumbnail_dimensions=self.thumnail_dimensions,
        )

        self.billing_window = BillingWindow(
            roll=path.name, path=path, num_images=self.image_manager.end_index
        )  # Initialises the Billing Window (found in GUI/bill.py)
        self.setup()
        self.image_manager.load_in_background()

    def setup(self):
        """
        Sets up the main "parent" window where all the "child" windows exist and operate
        If it helps understanding, this is kinda like HTML and CSS with it's div usage
        """

        self.parent = dpg.add_window(
            label=self.path.name,
            width=self.window_dimensions[0],
            height=self.window_dimensions[1],
            on_close=self.billing_window.close,  # Close the billing window along with the image window
        )

        with dpg.child_window(parent=self.parent):
            indicator = dpg.add_loading_indicator()  # This is the loading symbol lmao

            # this is an abomination, but it makes the window load 2 seconds faster

            ShittyMultiThreading(
                self.image_manager.load, (0, 1, self.image_manager.end_index - 1)
            ).start()
            image = self.image_manager.load(0)
            logger.debug(image.dpg_texture[3].shape)

            with dpg.texture_registry():
                # TODO: The next and previous image viewer could be changed into a scrollable selector
                # with all the images in them

                # Basically, instead of using the CPU, DPG uses the GPU to create the GUI you see.
                # It does this by uploading a "texture" with the image data to the GPU.
                # Check their documentation for more information.

                dpg.add_dynamic_texture(
                    # This is adding a texture for the main image
                    *self.main_image_dimensions,
                    default_value=image.dpg_texture[3],
                    tag=f"{self.parent}_Main Image",
                )

                next = self.image_manager.next()
                previous = self.image_manager.previous()

                dpg.add_dynamic_texture(
                    # This is adding a texture for the "Next Image"
                    *self.thumnail_dimensions,
                    default_value=next.thumbnail[3],
                    tag=f"{self.parent}_Next Image",
                )

                dpg.add_dynamic_texture(
                    # This is adding a texture for the "Previous Image"
                    *self.thumnail_dimensions,
                    default_value=previous.thumbnail[3],
                    tag=f"{self.parent}_Previous Image",
                )

            # Following code adds the next and previous buttons, as well as the slider to the window

            with dpg.group(horizontal=True):
                dpg.add_button(label="Next", callback=self.next)
                dpg.add_button(label="Previous", callback=self.previous)
                dpg.add_slider_int(
                    default_value=1,
                    min_value=1,
                    max_value=self.image_manager.end_index,
                    callback=lambda _, a, u: self.open(a - 1),
                    tag=f"{self.parent}_Image Slider",
                )

            # Finally, the following code adds the images to the window

            with dpg.group(horizontal=True):
                with dpg.group():
                    dpg.add_image(f"{self.parent}_Previous Image")
                    dpg.add_image(f"{self.parent}_Next Image")
                dpg.add_image(f"{self.parent}_Main Image")
            dpg.delete_item(indicator)

            # Masti sauce

    def open(self, index: int):
        """
        Renders the images onto the window based on an index.
        The index can be altered via functions next() and previous()

        Args:
            index (int): This is the index number of the image in the roll

        Returns:

        """
        self.current_image = index

        image = self.image_manager.load(index)
        previous = self.image_manager.previous()
        next = self.image_manager.next()

        dpg.set_value(f"{self.parent}_Main Image", image.dpg_texture[3])
        dpg.set_value(f"{self.parent}_Next Image", next.thumbnail[3])
        dpg.set_value(f"{self.parent}_Previous Image", previous.thumbnail[3])
        dpg.set_value(f"{self.parent}_Image Slider", self.current_image + 1)
        self.billing_window.load(index)

    def next(self):
        """
        Goes to the next image when the "next" button is pressed
        """
        if self.current_image < self.image_manager.end_index - 1:
            self.open(self.current_image + 1)
        else:
            self.open(0)

    def previous(self):
        """
        Goes to the previous image when the "previous" button is pressed
        """
        if self.current_image > 0:
            self.open(self.current_image - 1)
        else:
            self.open(self.image_manager.end_index - 1)
