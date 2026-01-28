import logging
import json
import time
from pathlib import Path
import functools
import itertools

from dearpygui import dearpygui as dpg
from collections import defaultdict, deque

from abc import ABC, ABCMeta, abstractmethod
from dataclasses import dataclass
from typing import Any, Callable, Literal
from uuid import uuid1

logger = logging.getLogger("GUI.GraphABC")


def natural_time(time_in_seconds: float) -> str:
    """
    Converts a time in seconds to a 6-padded scaled unit
    E.g.:
        1.5000 ->   1.50  s
        0.1000 -> 100.00 ms
        0.0001 -> 100.00 us
    """
    units = (
        ("mi", 60),
        (" s", 1),
        ("ms", 1e-3),
        ("us", 1e-6),
    )
    # TODO: Add utf-8 character set to dearpygui so that greek letters can be displayed
    absolute = abs(time_in_seconds)

    for label, size in units:
        if absolute > size:
            return f"{time_in_seconds / size:.2f} {label}"

    return f"{time_in_seconds / 1e-9:.2f} ns"


def update_exec_time(func):
    @functools.wraps(func)
    def wrapper(self, *args, **kwargs):
        if not self.visual_mode:
            return
        dpg.show_item(self.loading)
        start = time.perf_counter()
        result = func(self, *args, **kwargs)
        end = time.perf_counter()
        dpg.hide_item(self.loading)
        dpg.set_value(self.processing_time, natural_time(end - start))
        return result

    return wrapper


class TimedMeta(ABCMeta):
    def __init__(cls, name, bases, namespace):
        super().__init__(name, bases, namespace)
        # Wrap all methods that override abstract ones
        for base in bases:
            for attr_name, attr_val in base.__dict__.items():
                if getattr(attr_val, "__isabstractmethod__", False):
                    if attr_name in namespace:
                        method = getattr(cls, attr_name)
                        setattr(cls, attr_name, update_exec_time(method))


class TwoWayDict(dict):
    def __setitem__(self, key, value):
        if key in self:
            del self[key]
        if value in self:
            del self[value]
        dict.__setitem__(self, key, value)
        dict.__setitem__(self, value, key)

    def __delitem__(self, key):
        dict.__delitem__(self, self[key])
        dict.__delitem__(self, key)

    def __len__(self):
        """Returns the number of connections"""
        return dict.__len__(self) // 2


@dataclass
class Edge:
    id: str | int
    data: Any
    input: "Node"
    output: "Node"
    input_attribute_id: str | int
    output_attribute_id: str | int

    def connect(self):
        if self.output.validate_input(
            self, self.input_attribute_id
        ) and self.input.validate_output(self, self.output_attribute_id):
            self.input.add_output(self, self.input_attribute_id)
            self.output.add_input(self, self.output_attribute_id)
            logger.debug(f"Connected {self.input} to {self.output} via {self}")
            return True
        logger.warning(f"Failed to connect {self.input} to {self.output} via {self}")
        return False

    def disconnect(self):
        # DO NOT CHANGE THE ORDER IN WHICH THESE FUNCTIONS ARE CALLED
        self.input.remove_output(self, self.input_attribute_id)
        self.output.remove_input(self, self.output_attribute_id)

    def to_dict(self):
        values = {
            "input": self.input.uuid,
            "output": self.output.uuid,
            "input_attr": self.input.output_attributes_id_lookup[
                self.input_attribute_id
            ],
            "output_attr": self.output.input_attributes_id_lookup[
                self.output_attribute_id
            ],
        }
        return values


class EdgeGui(Edge):
    def __init__(
        self, id, data, input, output, input_attribute_id, output_attribute_id
    ):
        super().__init__(
            id, data, input, output, input_attribute_id, output_attribute_id
        )

    def connect(self):
        if super().connect():
            return True
        dpg.delete_item(self.id)
        return False

    def disconnect(self):
        # DO NOT CHANGE THE ORDER IN WHICH THESE FUNCTIONS ARE CALLED
        super().disconnect()
        dpg.delete_item(self.id)


class Node(ABC, metaclass=TimedMeta):
    """
    Node do the processing, Edges store the data
    """

    REGISTRY = {}

    def __init__(
        self,
        label: str,
        is_inspect: bool = False,
        uuid: str | None = None,
        settings={},
    ):
        self.id = None
        self.label = label
        self.input_attributes: dict[str | int, list[Edge]] = {}
        self.output_attributes: dict[str | int, list[Edge]] = {}
        self.input_attributes_id_lookup: TwoWayDict = TwoWayDict()
        self.output_attributes_id_lookup: TwoWayDict = TwoWayDict()
        self.update_hook = lambda: None
        self.delete_hook = lambda: None
        self.is_inspect = is_inspect
        if not uuid:
            self.uuid = str(uuid1())
        else:
            self.uuid = uuid
        self.state: Literal[0, 1] = 0
        self.settings: dict = settings
        self.visual_mode = False

    def delete(self):
        self.delete_hook()
        if self.id:
            dpg.delete_item(self.id)

    def init_dpg(
        self,
        parent: str | int,
        update_hook: Callable = lambda: None,
        delete_hook: Callable = lambda: None,
    ):
        """
        Make the Node visible
        """
        self.visual_mode = True
        self.id = dpg.add_node(label=self.label, parent=parent)
        static = dpg.add_node_attribute(
            attribute_type=dpg.mvNode_Attr_Static, parent=self.id
        )
        self.status_group = dpg.add_group(horizontal=True, parent=static)
        dpg.add_button(
            label="Close",
            callback=self.delete,
            parent=self.status_group,
            width=50,
            height=24,
        )
        self.processing_time = dpg.add_text("", parent=self.status_group)
        self.loading = dpg.add_text("(>_<)", parent=self.status_group, show=False)
        self.update_hook = update_hook
        self.delete_hook = delete_hook

    def add_attribute(self, label, attribute_type):
        if self.visual_mode and self.id:
            attribute_id = dpg.add_node_attribute(
                parent=self.id, label=label, attribute_type=attribute_type
            )
        else:
            attribute_id = str(uuid1())
        if attribute_type == dpg.mvNode_Attr_Input:
            self.input_attributes[attribute_id] = []
            self.input_attributes_id_lookup[attribute_id] = label
        elif attribute_type == dpg.mvNode_Attr_Output:
            self.output_attributes[attribute_id] = []
            self.output_attributes_id_lookup[attribute_id] = label

        logger.debug(
            f"Attribute lists for {self.label} is {self.input_attributes} and {self.output_attributes}"
        )

        return attribute_id

    @abstractmethod
    def setup_attributes(self):
        pass

    @abstractmethod
    def update_settings(self):
        pass

    @abstractmethod
    def process(self, is_final=False):
        """
        It's only job is to populate all output edges
        """
        self.update_settings()

    def add_input(self, edge: Edge, attribute_id):
        self.input_attributes[attribute_id].append(edge)
        self.update()

    def add_output(self, edge: Edge, attribute_id):
        self.activate()
        self.output_attributes[attribute_id].append(edge)

    def remove_input(self, edge: Edge, attribute_id):
        self.activate()
        self.input_attributes[attribute_id].remove(edge)

    def remove_output(self, edge: Edge, attribute_id):
        self.output_attributes[attribute_id].remove(edge)
        self.update()

    def update(self):
        self.activate()
        self.update_hook()

    def activate(self):
        self.state = 1
        logger.debug(str(self))
        for attribute in self.output_attributes.values():
            for edge in attribute:
                edge.output.activate()

    def validate_input(self, edge, attribute_id) -> bool:
        return True

    def validate_output(self, edge, attribute_id) -> bool:
        return True

    def __init_subclass__(cls):
        Node.REGISTRY[cls.__name__] = cls

    def to_dict(self):
        return {
            "type": type(self).__name__,
            "settings": self.settings,
            "output_edges": [
                edge.to_dict()
                for edges in self.output_attributes.values()
                for edge in edges
            ],
        }


class Graph:
    def __init__(self) -> None:
        self.node_lookup_by_attribute_id = {}
        self.node_lookup_by_uuid = {}
        self.edge_lookup_by_edge_id = {}
        self.adjacency_list: dict[Node, list[Node]] = {}

    def get_visible_nodes(self):
        """
        Get a list of nodes that are eventually connected to an inspect_node
        """
        inspect_nodes = [node for node in self.adjacency_list if node.is_inspect]
        visible_nodes = set(inspect_nodes)
        q = deque(inspect_nodes)
        while q:
            node = q.popleft()
            for edge_list in node.input_attributes.values():
                for edge in list(edge_list):  # copy to avoid mutation problems
                    parent = edge.input
                    if parent not in visible_nodes:
                        visible_nodes.add(parent)
                        q.append(parent)
        return visible_nodes

    def add_node(self, node: Node):
        for attribute in itertools.chain(node.input_attributes, node.output_attributes):
            self.node_lookup_by_attribute_id[attribute] = node
            self.node_lookup_by_uuid[node.uuid] = node
        self.adjacency_list[node] = []

    def link(self, input: Node, output: Node, edge: EdgeGui):
        self.edge_lookup_by_edge_id[edge.id] = edge
        self.adjacency_list[input].append(output)
        logger.debug(self.edge_lookup_by_edge_id)
        edge.connect()

    def topological_sort(self, ignore_state=False):
        """
        Get a list of nodes to process in the correct order.
        (A will not be before B if the output of B is required for A)
        """
        # just kahn's algo: https://en.wikipedia.org/wiki/Topological_sorting
        in_degree = defaultdict(int)
        for node in self.adjacency_list:
            for neighbour in self.adjacency_list[node]:
                in_degree[neighbour] += 1

        queue = [node for node in self.adjacency_list if in_degree[node] == 0]
        dropped = []
        if not ignore_state:
            dropped = [node for node in self.adjacency_list if node.state == 0]
        sorted_list = []

        while queue:
            node = queue.pop()
            if ignore_state:
                sorted_list.append(node)
            elif node.state == 1:
                sorted_list.append(node)
            logger.debug(f"{node}, adjecency - {self.adjacency_list[node]}")
            for neighbour in self.adjacency_list[node]:
                in_degree[neighbour] -= 1
                if in_degree[neighbour] == 0:
                    queue.append(neighbour)

        logger.debug(f"Dropped nodes: {dropped}")
        logger.debug(f"Execution order: {sorted_list}")

        if len(sorted_list) + len(dropped) != len(self.adjacency_list):
            logger.error("There is a cycle in your graph!!!")
            return []
        else:
            return sorted_list

    def evaluate(self, is_final=False):
        visible_nodes = self.get_visible_nodes()
        sorted_node_list = self.topological_sort(ignore_state=is_final)

        logger.debug(
            f"Sorted node list: {sorted_node_list}, Visible Nodes: {visible_nodes}"
        )
        for node in sorted_node_list:
            if node in visible_nodes:
                logger.debug(f"Processed Node {node}")
                node.process(is_final=is_final)
                node.state = 0

        # now reactivate the whole graph and run it again with scaled down values so that we can make sure that the app doesn't crash
        logger.info("Graph Executed! Rerunning with scaled down values")
        if is_final:
            for node in self.topological_sort(ignore_state=True):
                node.state = 1
            self.evaluate()

    def save(self, filename: str):
        """
        Saves the graph structure to a form that can be loaded
        """
        graph = {
            "Nodes": {i.uuid: i.to_dict() for i in self.get_visible_nodes()},
        }
        with open(Path(f"./Data/Workflows/{filename}.json"), "w") as file:
            json.dump(graph, file)

    def load_nodes(self, filename: str, visual_mode=False):
        with open(Path(f"./Data/Workflows/{filename}"), "r") as file:
            graph = json.load(file)

        for uuid, node in graph["Nodes"].items():
            if node["type"] == "ImageNode":
                continue
            constructed_node: Node = Node.REGISTRY[node["type"]](
                settings=node["settings"], uuid=uuid
            )
            if not visual_mode:
                self.add_node(constructed_node)
            yield constructed_node

    def load_node_output_attributes(self, filename: str, visual_mode=False):
        with open(Path(f"./Data/Workflows/{filename}"), "r") as file:
            graph = json.load(file)

        for node in graph["Nodes"].values():
            if node["type"] == "ImageNode":
                continue
            yield node["output_edges"]
