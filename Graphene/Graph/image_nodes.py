import logging

import dearpygui.dearpygui as dpg

from Application import Image

from .graph_abc import Node

logger = logging.getLogger("GUI.ImageNodes")


class ImageNode(Node):
    def __init__(self, image: Image, label="Import", is_inspect=False, **kwargs):
        super().__init__(label, is_inspect, **kwargs)
        self.image = image

    def update_settings(self):
        return super().update_settings()

    def setup_attributes(self):
        super().setup_attributes()
        self.image_attribute = self.add_attribute(
            label="Image", attribute_type=dpg.mvNode_Attr_Output
        )
        if self.visual_mode:
            with dpg.texture_registry():
                dpg.add_dynamic_texture(
                    *self.image.thumbnail_dimensions,
                    default_value=self.image.thumbnail,
                    tag=f"{self.id}_image",
                )
                logger.debug("Added entry to texture_registry")
            dpg.add_image(f"{self.id}_image", parent=self.image_attribute)
        logger.debug("Added image to node")

    def process(self, is_final=False):
        # put the image in all connected output edges
        super().process()
        for edge in self.output_attributes[self.image_attribute]:
            # TODO: adjust scaling so that the image isn't grainy
            edge.data = self.image if is_final else self.image.get_scaled_image()
            logger.debug(f"Populated edge {edge.id} with image from {self.id}")
