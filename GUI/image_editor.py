import logging
from abc import ABC, abstractmethod
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import dearpygui.dearpygui as dpg

import Application.image_processing as ImageTools
from Application import Image, ImageManager

logger = logging.getLogger("GUI.Editor")


@dataclass
class Edge:
    incoming: "Node"
    outgoing: "Node"
    incoming_attribute: str | int
    outgoing_attribute: str | int
    link_id: str | int
    data: Any

    def delete(self):
        self.incoming.attributes[self.incoming_attribute].remove(self)
        self.outgoing.attributes[self.outgoing_attribute].remove(self)


class Node(ABC):
    def __init__(self, label: str, parent: str) -> None:
        self.id = dpg.add_node(label=label, parent=parent)
        self.label = label
        self.attributes = {}
        self.attribute_lookup = {}

    @abstractmethod
    def process(self):
        """
        process all incoming data and give out an output to go onto the next node
        """
        logger.debug(f"Processing {self.label} Node {self.id}")

    @abstractmethod
    def connect_input(self, edge: Edge, attribute) -> bool:
        if not attribute in self.attributes:
            # initialise a list of attributes
            self.attributes[attribute] = [
                edge,
            ]
        else:
            # appaned to the list of connected vertices for that attribute
            self.attributes[attribute].append(edge)

    @abstractmethod
    def connect_output(self, edge: Edge, attribute) -> bool:
        if not attribute in self.attributes:
            # initialise a list of attributes
            self.attributes[attribute] = [
                edge,
            ]
        else:
            # appaned to the list of connected vertices for that attribute
            self.attributes[attribute].append(edge)


class HistogramNode(Node):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        with dpg.node_attribute(
            shape=dpg.mvNode_PinShape_TriangleFilled,
            parent=self.id,
        ) as self.image_attribute:
            dpg.add_text("Hello Chat")
        self.attribute_lookup[self.image_attribute] = "Image Source"

    def process(self):
        return super().process()

    def connect_input(self, edge: Edge, attribute) -> bool:
        if (
            self.image_attribute not in self.attributes
            or not self.attributes[attribute]
        ):
            self.attributes[self.image_attribute] = [
                edge,
            ]
            return True
        return False

    def connect_output(self, edge: Edge, attribute) -> bool:
        return super().connect_output(edge, attribute)


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
        self.attribute_lookup[self.image_attribute] = "Image Source"

    def process(self):
        return super().process()

    def connect_output(self, edge: Edge, attribute) -> bool:
        return super().connect_output(edge, attribute)

    def connect_input(self, edge: Edge, attribute) -> bool:
        return super().connect_input(edge, attribute)


class EditingWindow:
    def __init__(self, source: list[Path]) -> None:
        self.image_manager = ImageManager.from_file_list(
            source, (600, 600), thumbnail_dimensions=(200, 200)
        )

        self.adjaceny_list = []
        self.attribute_lookup = {}
        self.edge_lookup = {}

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
        edge_id = dpg.add_node_link(app_data[0], app_data[1], parent=sender)
        edge = Edge(app_data[0], app_data[1], from_node, to_node, edge_id, None)

        # validate the connection
        if not from_node.connect_output(edge, app_data[0]) and to_node.connect_input(
            edge, app_data[1]
        ):
            dpg.delete_item(edge_id)
            self.attribute_lookup[from_node].disconnect_edge(edge)
            self.attribute_lookup[to_node].disconnect_edge(edge)
            logger.debug(f"Did not connect edge!")
            return

        self.edge_lookup[edge_id] = edge
        logger.debug(
            f"Connected {from_node} ({app_data[0]}) to {to_node} ({app_data[1]}) via edge: {edge_id}"
        )

    def delink(self, sender, app_data):
        # TODO: implement node deletion
        logger.debug(f"{sender} {app_data}")
        self.edge_lookup[app_data].delete()
        dpg.delete_item(app_data)

    def add_node(self, node: Node):
        self.adjaceny_list.append(node)
        for attribute in node.attribute_lookup.keys():
            self.attribute_lookup[attribute] = node
        logger.debug(node.attribute_lookup)

    def add_histogram_node(self):
        histogram = HistogramNode(label="Histogram", parent="Image Editor")
        self.add_node(histogram)

    def add_image_node(self):
        image = ImageNode(
            label="Image", parent="Image Editor", image=self.image_manager.load(0)
        )
        self.add_node(image)

    def evaluate(self):
        # TODO: there are two ways of doing this
        # 1. perform a topological sort and evaluate nodes
        # 2. start from the ending nodes and evaluate edges recursively

        # TODO: CHECK FOR CYCLES WHILE YOU ARE AT IT, if we are gonna check for cycles anyways, might as well sort it tbh
        pass
