import functools
import logging
from pathlib import Path

import dearpygui.dearpygui as dpg

from Application import Image, get_histogram

from .graph_abc import Node

logger = logging.getLogger("GUI.InspectNodes")


@functools.cache
def set_up_line_plot_themes():
    with dpg.theme() as r:
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_color(
                dpg.mvPlotCol_Line, value=(255, 0, 0), category=dpg.mvThemeCat_Plots
            )
    with dpg.theme() as g:
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_color(
                dpg.mvPlotCol_Line, value=(0, 255, 0), category=dpg.mvThemeCat_Plots
            )
    with dpg.theme() as b:
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_color(
                dpg.mvPlotCol_Line, value=(0, 0, 255), category=dpg.mvThemeCat_Plots
            )
    return r, g, b


class HistogramNode(Node):
    def __init__(self, label="Histogram", is_inspect=True, **kwargs):
        super().__init__(label, is_inspect, **kwargs)

    def setup_attributes(self):
        super().setup_attributes()
        self.image_attribute = self.add_attribute(
            label="Image", attribute_type=dpg.mvNode_Attr_Input
        )
        if self.visual_mode:
            with dpg.plot(height=200, width=200, parent=self.image_attribute):
                dpg.add_plot_axis(dpg.mvXAxis, label="Value", no_label=True)
                dpg.add_plot_axis(
                    dpg.mvYAxis,
                    label="Count",
                    tag=f"{self.id}_yaxis",
                    no_label=True,
                    auto_fit=True,
                    no_tick_labels=True,
                )
                r, g, b = set_up_line_plot_themes()
                dpg.add_line_series(
                    [i for i in range(256)],
                    [
                        0.0,
                    ]
                    * 256,
                    tag=f"{self.id}_R",
                    parent=f"{self.id}_yaxis",
                    label="R",
                )
                dpg.add_line_series(
                    [i for i in range(256)],
                    [
                        0.0,
                    ]
                    * 256,
                    tag=f"{self.id}_G",
                    parent=f"{self.id}_yaxis",
                    label="G",
                )
                dpg.add_line_series(
                    [i for i in range(256)],
                    [
                        0.0,
                    ]
                    * 256,
                    tag=f"{self.id}_B",
                    parent=f"{self.id}_yaxis",
                    label="B",
                )
                dpg.bind_item_theme(f"{self.id}_R", r)
                dpg.bind_item_theme(f"{self.id}_G", g)
                dpg.bind_item_theme(f"{self.id}_B", b)
                dpg.add_plot_legend()
        logger.debug("Initialised histogram node")

    def update_settings(self):
        return super().update_settings()

    def process(self, is_final=False):
        super().process(is_final)
        if not self.input_attributes[self.image_attribute]:
            return
        edge = self.input_attributes[self.image_attribute][0]
        if not edge.data:
            return
        image: Image = edge.data
        if self.visual_mode:
            histogram = get_histogram(image.raw_image)
            dpg.set_value(f"{self.id}_R", [[i for i in range(256)], histogram[0]])
            dpg.set_value(f"{self.id}_G", [[i for i in range(256)], histogram[1]])
            dpg.set_value(f"{self.id}_B", [[i for i in range(256)], histogram[2]])
            logger.debug(f"Processed histogram in histogram node {self.id}")

    def validate_input(self, edge, attribute_id) -> bool:
        # only permitting a single connection
        if not self.image_attribute:
            return False
        if self.input_attributes[self.image_attribute]:
            logger.warning(
                "Invalid! You can only connect one image node to histogram node"
            )
            return False
        return True


@functools.cache
def get_default_image():
    return Image.frompath(Path(f"./Data/default.png"), (600, 600), (200, 200))


class PreviewNode(Node):
    def __init__(self, label="Preview", is_inspect=True, **kwargs):
        super().__init__(label, is_inspect, **kwargs)
        self.image: Image = get_default_image().get_scaled_image()

    def setup_attributes(self):
        self.image_attribute = self.add_attribute(
            label="Image", attribute_type=dpg.mvNode_Attr_Input
        )
        self.image_output_attribute = self.add_attribute(
            label="Out", attribute_type=dpg.mvNode_Attr_Output
        )
        if self.visual_mode:
            with dpg.plot(
                no_frame=True, width=400, height=300, parent=self.image_attribute
            ) as self.plot:
                self.xaxis = dpg.add_plot_axis(dpg.mvXAxis, no_tick_labels=True)
                with dpg.plot_axis(dpg.mvYAxis, no_tick_labels=True) as self.yaxis:
                    self.register_and_show_image(self.image, self.yaxis)
                logger.debug("Added default image to preview node")

    def validate_input(self, edge, attribute_id) -> bool:
        # only permitting a single connection
        if self.input_attributes[self.image_attribute]:
            logger.warning(
                "Invalid! You can only connect one image node to preview node"
            )
            return False
        return True

    def register_and_show_image(self, image: Image, parent: str | int):
        # remember to delete any pre_existing image_series and textures
        with dpg.texture_registry():
            size = image.raw_image.size
            dpg.add_dynamic_texture(
                *size,
                default_value=image.dpg_raw,
                tag=f"{self.id}_image",
            )
        logger.debug("Added entry to texture_registry")
        dpg.add_image_series(
            f"{self.id}_image",
            [0, 0],
            image.raw_image.size,
            parent=parent,
            tag=f"{self.id}_image_series",
        )
        dpg.fit_axis_data(self.yaxis)
        width, height = image.raw_image.width, image.raw_image.height
        ratio = width / height
        dpg.set_item_width(self.plot, int(ratio * 300))
        dpg.fit_axis_data(self.xaxis)

    def update_settings(self):
        return super().update_settings()

    def process(self, is_final=False):
        if self.input_attributes[self.image_attribute]:
            edge = self.input_attributes[self.image_attribute][0]
            image: Image = edge.data
            if image and self.visual_mode:
                if self.image.raw_image.size != image.raw_image.size:
                    dpg.delete_item(f"{self.id}_image")
                    dpg.delete_item(f"{self.id}_image_series")
                    self.register_and_show_image(image, parent=self.yaxis)
                else:
                    dpg.set_value(f"{self.id}_image", image.dpg_raw)
            if image:
                self.image = image
            if is_final:
                self.image.raw_image.save(f"./Data/{self.id}.png")
                logger.debug(f"Saved output to {self.id}.png")

            for edge in self.output_attributes[self.image_output_attribute]:
                edge.data = self.image
                logger.debug(f"Populated edge {edge.id} with image from {self.id}")
