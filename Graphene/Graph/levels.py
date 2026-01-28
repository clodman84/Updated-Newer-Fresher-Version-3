import functools
import logging

import dearpygui.dearpygui as dpg
from line_profiler import profile

from Application import Image, get_rgb_histogram, levels

from .graph_abc import Node

logger = logging.getLogger("GUI.Levels")


@functools.cache
def set_up_line_plot_themes():
    with dpg.theme() as luma_theme:
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_color(
                dpg.mvPlotCol_Line, value=(255, 255, 255), category=dpg.mvThemeCat_Plots
            )
    return luma_theme


class Levels(Node):
    def __init__(self, label="Levels", is_inspect=False, **kwargs):
        super().__init__(label, is_inspect, **kwargs)
        if not self.settings:
            self.settings = {"black_level": 0, "white_level": 1, "gamma": 1}

    def setup_attributes(self):
        self.image_attribute = self.add_attribute(
            label="Image", attribute_type=dpg.mvNode_Attr_Input
        )
        self.image_output_attribute = self.add_attribute(
            label="Out", attribute_type=dpg.mvNode_Attr_Output
        )
        if self.visual_mode:
            with dpg.group(parent=self.image_attribute, width=400, height=240):
                with dpg.plot(height=200, width=-1):
                    dpg.add_plot_axis(dpg.mvXAxis, label="Value", no_label=True)
                    dpg.add_plot_axis(
                        dpg.mvYAxis,
                        label="Count",
                        tag=f"{self.id}_yaxis",
                        no_label=True,
                        auto_fit=True,
                        no_tick_labels=True,
                    )

                    luma_theme = set_up_line_plot_themes()
                    dpg.add_line_series(
                        [i for i in range(256)],
                        [
                            0.0,
                        ]
                        * 256,
                        tag=f"{self.id}_luma",
                        parent=f"{self.id}_yaxis",
                    )
                    dpg.bind_item_theme(f"{self.id}_luma", luma_theme)
                    dpg.add_plot_legend()
                    self.black_level = dpg.add_drag_line(
                        vertical=True,
                        default_value=self.settings["black_level"] * 255,
                        color=[5, 32, 46],
                        thickness=3,
                        callback=self.update,
                    )

                    self.white_level = dpg.add_drag_line(
                        vertical=True,
                        default_value=self.settings["white_level"] * 255,
                        color=[209, 236, 250],
                        thickness=3,
                        callback=self.update,
                    )

                self.gamma = dpg.add_input_float(
                    width=-1,
                    default_value=self.settings["gamma"],
                    max_clamped=True,
                    min_clamped=True,
                    max_value=9.99,
                    min_value=0.01,
                    callback=self.update,
                )

    def validate_input(self, edge, attribute_id) -> bool:
        # only permitting a single connection
        if self.input_attributes[self.image_attribute]:
            logger.warning(
                "Invalid! You can only connect one image node to preview node"
            )
            return False
        return True

    def update_settings(self):
        if self.visual_mode:
            self.settings["black_level"] = dpg.get_value(self.black_level) / 255
            self.settings["white_level"] = dpg.get_value(self.white_level) / 255
            self.settings["gamma"] = dpg.get_value(self.gamma)

    @profile
    def process(self, is_final=False):
        super().process(is_final)
        if self.input_attributes[self.image_attribute]:
            edge = self.input_attributes[self.image_attribute][0]
            if not edge.data:
                return

            black = self.settings["black_level"]
            white = self.settings["white_level"]
            gamma = self.settings["gamma"]

            image: Image = edge.data

            if image:
                if self.visual_mode:
                    histogram = get_rgb_histogram(image.raw_image)
                    dpg.set_value(
                        f"{self.id}_luma", [[i for i in range(256)], histogram]
                    )

                updated_image = levels(image.raw_image, black, white, gamma)
                image = Image(image.path, updated_image, (600, 600), (200, 200))

            for edge in self.output_attributes[self.image_output_attribute]:
                edge.data = image
                logger.debug(f"Populated edge {edge.id} with image from {self.id}")
