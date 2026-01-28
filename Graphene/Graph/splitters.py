import logging
from typing import Callable

from dearpygui import dearpygui as dpg
from PIL import Image as PImage

from Application import Image, split_rgb, split_smh
from .graph_abc import Node

logger = logging.getLogger("GUI.Splitter")


class Splitter(Node):
    def __init__(
        self,
        label: str,
        splitter_func: Callable,
        channel_labels: list,
        is_inspect=False,
        **kwargs,
    ):
        super().__init__(label, is_inspect, **kwargs)
        self.splitter_func = splitter_func
        self.channel_labels = channel_labels
        self.channel_outs = {}
        self.channel_histogram = {}

    def setup_attributes(self):
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
                for channel in self.channel_labels:
                    line = dpg.add_line_series(
                        list(range(256)),
                        [
                            0.0,
                        ]
                        * 256,
                        parent=f"{self.id}_yaxis",
                        label=channel,
                    )
                    self.channel_histogram[channel] = line
                    dpg.add_plot_legend()

        for channel in self.channel_labels:
            attr = self.add_attribute(
                label=channel, attribute_type=dpg.mvNode_Attr_Output
            )
            self.channel_outs[channel] = attr
            if self.visual_mode:
                dpg.add_text(channel, parent=attr)

    def update_settings(self):
        pass

    def process(self, is_final=True):
        super().process()
        if not self.input_attributes[self.image_attribute]:
            return
        edge = self.input_attributes[self.image_attribute][0]
        if not edge.data:
            return
        image: Image = edge.data
        out: list[PImage.Image] = self.splitter_func(image.raw_image)

        for i, channel_name in enumerate(self.channel_labels):
            # compute and update histogram
            channel = out[i]
            if self.visual_mode:
                histogram = channel.convert("L").histogram()
                dpg.set_value(
                    self.channel_histogram[channel_name], [list(range(256)), histogram]
                )
            channel_attr = self.channel_outs[channel_name]
            for edge in self.output_attributes[channel_attr]:
                edge.data = Image(image.path, channel, (600, 600), (200, 200))

        logger.debug(f"Processed histogram in histogram node {self.id}")

    def validate_input(self, edge, attribute_id) -> bool:
        # only permitting a single connection
        if self.input_attributes[self.image_attribute]:
            logger.warning(
                "Invalid! You can only connect one image node to histogram node"
            )
            return False
        return True


class RGBSplitter(Splitter):
    def __init__(
        self,
        label="RGB Splitter",
        splitter_func=split_rgb,
        channel_labels=["Red", "Green", "Blue"],
        **kwargs,
    ):
        super().__init__(label, splitter_func, channel_labels, **kwargs)


class SMHSplitter(Splitter):
    def __init__(
        self,
        label="SMH Splitter",
        splitter_func=split_smh,
        channel_labels=["Shadows", "Midtones", "Highlights"],
        **kwargs,
    ):
        super().__init__(label, splitter_func, channel_labels, **kwargs)
