import dearpygui.dearpygui as dpg

from Application import ImageManager, SimpleTimer

from .bill import BillingWindow


class ImageWindow:
    def __init__(
        self, parent, billing_window: BillingWindow, image_manager: ImageManager
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
        self.billing_window = billing_window
        self.image_manager = image_manager
        self.setup(parent)
        with SimpleTimer(process_name="Loading all images in the background", log=True):
            self.image_manager.load_in_background()

    def setup(self, parent):
        with dpg.child_window(parent=parent):
            image = self.image_manager.load(0)
            with dpg.texture_registry(show=True):
                dpg.add_dynamic_texture(
                    750, 500, default_value=image.dpg_texture[3], tag="Main Image"
                )
                next = self.image_manager.next()
                previous = self.image_manager.previous()
                dpg.add_dynamic_texture(
                    240, 240, default_value=next.thumbnail[3], tag="Next Image"
                )
                dpg.add_dynamic_texture(
                    240, 240, default_value=previous.thumbnail[3], tag="Previous Image"
                )

            with dpg.group(horizontal=True):
                dpg.add_button(label="Next", callback=self.next)
                dpg.add_button(label="Previous", callback=self.previous)
                dpg.add_text(f"Image {self.current_image + 1}/40", tag="Image Number")

            with dpg.group(horizontal=True):
                with dpg.group():
                    dpg.add_text("Previous Image:")
                    dpg.add_image("Previous Image")
                    dpg.add_text("Next Image:")
                    dpg.add_image("Next Image")
                dpg.add_image("Main Image")

    def open(self, index: int):
        image = self.image_manager.load(index)
        previous = self.image_manager.previous()
        next = self.image_manager.next()
        dpg.set_value("Main Image", image.dpg_texture[3])
        dpg.set_value("Next Image", next.thumbnail[3])
        dpg.set_value("Previous Image", previous.thumbnail[3])
        dpg.set_value("Image Number", f"Image {self.current_image + 1}/40")
        self.billing_window.load(index)

    def next(self):
        if self.current_image < 39:
            self.current_image += 1
        else:
            self.current_image = 0
        self.open(self.current_image)

    def previous(self):
        if self.current_image > 0:
            self.current_image -= 1
        else:
            self.current_image = 39
        self.open(self.current_image)
