import logging
from abc import ABC
from pathlib import Path
from typing import Type

import dearpygui.dearpygui as dpg

import Application.image_processing as ImageTools
from Application import Image, ImageManager

logger = logging.getLogger("GUI.Editor")


class Node(ABC):
    def __init__(self, label: str, parent: str) -> None:
        """
        Its the job of the subclass to implement the attributes for a node

        """
        self.id = dpg.add_node(label=label, parent=parent)
        self.incoming = []
        self.outgoing = []

    def process(self):
        """
        process all incoming data and give out an output to go onto the next node
        """
        pass

    @property
    def attributes(self):
        return []

    def register_link(self, source, destination, attibute) -> bool:
        return True

    def pull_attribute(self, attibute):
        pass


class HistogramNode(Node):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        with dpg.node_attribute(
            shape=dpg.mvNode_PinShape_TriangleFilled,
            parent=self.id,
        ) as self.histogram_attribute:
            dpg.add_text("Hello Chat")
            # TODO: Implement actual data transport and histograms

    @property
    def attributes(self):
        return [
            self.histogram_attribute,
        ]

    def register_link(self, source, destination, attibute) -> bool:
        if not self.incoming:
            self.incoming.append(destination)
            return True
        return False


class ImageNode(Node):
    def __init__(self, *args, image: Image, **kwargs):
        super().__init__(*args, **kwargs)
        self.image = image
        with dpg.texture_registry():
            # TODO: The next and previous image viewer could be changed into a scrollable selector
            # with all the images in them
            dpg.add_dynamic_texture(
                200,
                200,
                default_value=image.thumbnail[3],
                tag=f"{self.id}_image",
            )
            logger.debug("added entry to texture_registry")
        with dpg.node_attribute(
            shape=dpg.mvNode_PinShape_TriangleFilled,
            parent=self.id,
            attribute_type=dpg.mvNode_Attr_Output,
        ) as self.image_attribute:
            logger.debug("Adding image to node")
            with dpg.child_window(width=200, height=200):
                dpg.add_image(f"{self.id}_image")
                logger.debug("Added image to node")

    @property
    def attributes(self):
        return [
            self.image_attribute,
        ]

    def register_link(self, source, destination, attibute) -> bool:
        self.outgoing.append(destination)
        return True


class EditingWindow:
    def __init__(self, source: list[Path]) -> None:
        self.image_manager = ImageManager.from_file_list(
            source, (600, 600), thumbnail_dimensions=(200, 200)
        )
        self.adjaceny_list = []
        self.attribute_lookup = (
            {}
        )  # dict of attribute ids and their corresponding nodes
        with dpg.window(label="Image Editor", width=500, height=500):
            with dpg.menu_bar():
                with dpg.menu(label="Inspect"):
                    dpg.add_menu_item(
                        label="Histogram", callback=self.add_histogram_node
                    )
                with dpg.menu(label="Import"):
                    dpg.add_menu_item(label="Image", callback=self.add_image_node)
            with dpg.node_editor(
                callback=self.link, delink_callback=self.delink, tag="Image Editor"
            ):
                pass

    def link(self, sender, app_data):
        from_node = self.attribute_lookup[app_data[0]]
        to_node = self.attribute_lookup[app_data[1]]
        if from_node.register_link(
            from_node, to_node, app_data[0]
        ) and to_node.register_link(from_node, to_node, app_data[1]):
            dpg.add_node_link(app_data[0], app_data[1], parent=sender)
            logger.debug(
                f"Connected {from_node}({app_data[0]}) to {to_node}({app_data[1]})"
            )

    def delink(self, sender, app_data):
        # TODO: implement node deletion
        logger.debug(f"{sender} {app_data}")
        dpg.delete_item(app_data)

    def add_node(self, node: Node):
        self.adjaceny_list.append(node)
        for attribute in node.attributes:
            self.attribute_lookup[attribute] = node

    def add_histogram_node(self):
        histogram = HistogramNode(label="Histogram", parent="Image Editor")
        self.add_node(histogram)

    def add_image_node(self):
        image = ImageNode(
            label="Image", parent="Image Editor", image=self.image_manager.load(0)
        )
        self.add_node(image)
