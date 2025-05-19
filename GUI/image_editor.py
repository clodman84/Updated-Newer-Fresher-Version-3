import logging
from abc import ABC
from pathlib import Path

import dearpygui.dearpygui as dpg

from Application import ImageManager

logger = logging.getLogger("GUI.Editor")


class Node(ABC):
    def __init__(self, label: str, parent: str) -> None:
        """
        Its the job of the subclass to implement the attributes for a node

        """
        self.id = dpg.add_node(label=label, parent=parent)

    def process(self):
        """
        process all incoming data and give out an output to go onto the next node
        """
        pass


class HistogramNode(Node):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        with dpg.node_attribute(
            shape=dpg.mvNode_PinShape_TriangleFilled,
            parent=self.id,
        ):
            self.graph = dpg.add_histogram_series(x=[100.0] * 50)


class ImageNode(Node):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        with dpg.node_attribute(
            shape=dpg.mvNode_PinShape_TriangleFilled,
            parent=self.id,
            attribute_type=dpg.mvNode_Attr_Output,
        ):
            with dpg.child_window(width=150, height=150):
                dpg.add_text("Imagine an image here")


class EditingWindow:
    def __init__(self, source: list[Path]) -> None:
        # TODO: Implement an adjacency list and linking rules governed by attributes themselves
        self.image_manager = ImageManager.from_file_list(
            source, (600, 600), thumbnail_dimensions=(100, 100)
        )
        with dpg.window(label="Image Editor", width=500, height=500):
            with dpg.menu_bar():
                with dpg.menu(label="Inspect"):
                    dpg.add_menu_item(
                        label="Histogram",
                        callback=lambda: HistogramNode(
                            label="Histogram", parent="Image Editor"
                        ),
                    )
                with dpg.menu(label="Import"):
                    dpg.add_menu_item(
                        label="Image",
                        callback=lambda: ImageNode(
                            label="Image", parent="Image Editor"
                        ),
                    )
                with dpg.menu(label="Edit"):
                    dpg.add_menu_item(
                        label="Image",
                        callback=lambda: ImageNode(
                            label="Image", parent="Image Editor"
                        ),
                    )
            with dpg.node_editor(
                callback=self.link, delink_callback=self.delink, tag="Image Editor"
            ):
                pass

    def link(self, sender, app_data):
        dpg.add_node_link(app_data[0], app_data[1], parent=sender)
        logger.debug(f"Connected {app_data} from {sender}")

    def delink(self, sender, app_data):
        dpg.delete_item(app_data)
