import logging

from dearpygui import dearpygui as dpg

from Application import Image, merge
from .graph_abc import Node

logger = logging.getLogger("GUI.Merge")


class Merge(Node):
    def __init__(self, label="Merge", is_inspect=False, **kwargs):
        super().__init__(label, is_inspect, **kwargs)

    def update_settings(self):
        return super().update_settings()

    def setup_attributes(self):
        self.image_attribute = self.add_attribute(
            label="Image", attribute_type=dpg.mvNode_Attr_Input
        )
        self.image_output_attribute = self.add_attribute(
            label="Out", attribute_type=dpg.mvNode_Attr_Output
        )
        if self.visual_mode:
            dpg.add_text(
                "Merely adds channels together", wrap=100, parent=self.image_attribute
            )

    def process(self, is_final=False):
        super().process(is_final)
        images = list(edge.data for edge in self.input_attributes[self.image_attribute])
        if not all(images):
            return
        out = Image(
            images[0].path,
            merge(image.raw_image for image in images),
            (600, 600),
            (200, 200),
        )
        for edge in self.output_attributes[self.image_output_attribute]:
            edge.data = out
            logger.debug(f"Populated edge {edge.id} with image from {self.id}")
