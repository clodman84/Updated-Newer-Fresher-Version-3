import logging
import dearpygui.dearpygui as dpg
from PIL import ImageEnhance

from Application import Image

from .graph_abc import Node

logger = logging.getLogger("GUI.EnhanceNodes")


class EnhanceNode(Node):
    def __init__(
        self,
        label: str,
        is_inspect=False,
        enhancement=lambda: None,
        **kwargs,
    ):
        super().__init__(label, is_inspect, **kwargs)
        if not self.settings:
            self.settings = {"value": 1}
        self.enhancement = enhancement

    def setup_attributes(self):
        self.image_attribute = self.add_attribute(
            label="Image", attribute_type=dpg.mvNode_Attr_Input
        )
        self.image_output_attribute = self.add_attribute(
            label="Out", attribute_type=dpg.mvNode_Attr_Output
        )
        if self.visual_mode:
            self.slider = dpg.add_input_float(
                parent=self.image_attribute,
                default_value=self.settings["value"],
                callback=self.update,
                width=200,
            )

    def update_settings(self):
        if self.visual_mode:
            self.settings["value"] = dpg.get_value(self.slider)

    def validate_input(self, edge, attribute_id) -> bool:
        # only permitting a single connection
        if self.input_attributes[self.image_attribute]:
            logger.warning(
                "Invalid! You can only connect one image node to preview node"
            )
            return False
        return True

    def process(self, is_final=False):
        super().process(is_final=is_final)
        if self.input_attributes[self.image_attribute]:
            edge = self.input_attributes[self.image_attribute][0]
            image: Image = edge.data
            if image:
                enhancer = self.enhancement(image.raw_image)
                factor = dpg.get_value(self.slider)
                updated_image = enhancer.enhance(factor=factor)
                image = Image(image.path, updated_image, (600, 600), (200, 200))

            for edge in self.output_attributes[self.image_output_attribute]:
                edge.data = image
                logger.debug(f"Populated edge {edge.id} with image from {self.id}")


class Saturation(EnhanceNode):
    def __init__(self, enhancement=ImageEnhance.Color, label="Saturation", **kwargs):
        super().__init__(label, enhancement=enhancement, **kwargs)


class Contrast(EnhanceNode):
    def __init__(self, enhancement=ImageEnhance.Contrast, label="Contrast", **kwargs):
        super().__init__(label, enhancement=enhancement, **kwargs)


class Brightness(EnhanceNode):
    def __init__(
        self, enhancement=ImageEnhance.Brightness, label="Brightness", **kwargs
    ):
        super().__init__(label, enhancement=enhancement, **kwargs)
