import itertools
import logging
import functools
from collections import defaultdict
from pathlib import Path
from typing import Callable

import dearpygui.dearpygui as dpg

from Application import Image
from Graphene.Graph.graph_abc import EdgeGui, Graph, Node
import Graphene.Graph as Nodes
from GUI.tablez import TableManager9000

logger = logging.getLogger("GUI.Editor")


class EditingWindow:
    def __init__(self, image: Image, on_close: Callable) -> None:
        self.graph = Graph()
        self.image = image
        with dpg.window(label="Image Editor", width=500, height=500, on_close=on_close):
            with dpg.menu_bar():
                with dpg.menu(label="Tools"):
                    dpg.add_menu_item(label="Save", callback=self.save_graph)
                    with dpg.tooltip(dpg.last_item()):
                        dpg.add_text("Save graph as a workflow")
                    dpg.add_menu_item(label="Load", callback=self.load_graph_window)
                    with dpg.tooltip(dpg.last_item()):
                        dpg.add_text("Load workflow")
                    dpg.add_menu_item(
                        label="Image",
                        callback=self.add_image_node,
                    )
                    with dpg.tooltip(dpg.last_item()):
                        dpg.add_text("Add an image source node")
                    dpg.add_menu_item(
                        label="Evaluate",
                        callback=lambda: self.graph.evaluate(is_final=True),
                    )
                    with dpg.tooltip(dpg.last_item()):
                        dpg.add_text(
                            "Run the entire node graph and update all outputs."
                        )

                with dpg.menu(label="Adjustments"):
                    dpg.add_menu_item(
                        label="Brightness", callback=self.add_brightness_node
                    )
                    with dpg.tooltip(dpg.last_item()):
                        dpg.add_text(
                            "Adjust overall lightness or darkness of the image."
                        )

                    dpg.add_menu_item(label="Contrast", callback=self.add_contrast_node)
                    with dpg.tooltip(dpg.last_item()):
                        dpg.add_text("Increase or decrease contrast between tones.")

                    dpg.add_menu_item(
                        label="Saturation", callback=self.add_saturation_node
                    )
                    with dpg.tooltip(dpg.last_item()):
                        dpg.add_text("Control colour intensity or vividness.")

                    dpg.add_menu_item(
                        label="Colour Balance", callback=self.add_colour_balance_node
                    )
                    with dpg.tooltip(dpg.last_item()):
                        dpg.add_text(
                            "Adjust colour in shadows, midtones and highlights independently."
                        )

                    dpg.add_menu_item(label="Levels", callback=self.add_levels_node)
                    with dpg.tooltip(dpg.last_item()):
                        dpg.add_text(
                            "Make dark things darker or light things lighter or both."
                        )

                with dpg.menu(label="Channel Ops"):
                    dpg.add_menu_item(
                        label="RGB Splitter", callback=self.add_rgb_splitter_node
                    )
                    with dpg.tooltip(dpg.last_item()):
                        dpg.add_text("Split image into red, green, and blue channels.")

                    dpg.add_menu_item(
                        label="Tone Splitter", callback=self.add_smh_splitter_node
                    )
                    with dpg.tooltip(dpg.last_item()):
                        dpg.add_text(
                            "Split image into shadows, midtones, and highlights."
                        )

                    dpg.add_menu_item(
                        label="Merge Channels", callback=self.add_merge_node
                    )
                    with dpg.tooltip(dpg.last_item()):
                        dpg.add_text(
                            "Merge separate channel outputs back into one image."
                        )

                with dpg.menu(label="View"):
                    dpg.add_menu_item(label="Preview", callback=self.add_preview_node)
                    with dpg.tooltip(dpg.last_item()):
                        dpg.add_text("Renders and saves the processed image.")

                    dpg.add_menu_item(
                        label="Histogram", callback=self.add_histogram_node
                    )
                    with dpg.tooltip(dpg.last_item()):
                        dpg.add_text(
                            "Displays RGB distribution of the connected image."
                        )
                dpg.add_menu_item(label="Auto Layout", callback=self.auto_arrange)

            with dpg.node_editor(
                callback=self.link, delink_callback=self.delink, minimap=True
            ) as self.node_editor:
                pass
            self.add_image_node()

    def auto_arrange(self):
        topo: list[Node] = self.graph.topological_sort(True)
        layer = {n: 0 for n in topo}
        x_gap = 50
        y_gap = 30

        for n in topo:
            for m in self.graph.adjacency_list[n]:
                layer[m] = max(layer[m], layer[n] + 1)
        columns = defaultdict(list)
        for n in topo:
            columns[layer[n]].append(n)
        x = 0
        positions = {}
        dpg.split_frame()
        dimensions = [
            [dpg.get_item_rect_size(node.id) for node in nodes]
            for nodes in columns.values()
        ]
        for i, node_list in enumerate(columns.values()):
            nodes = dimensions[i]
            max_width = max(node[0] for node in nodes)
            y = 0
            for j, node in enumerate(node_list):
                positions[node] = (x, y)
                y += nodes[j][1] + y_gap
            x += max_width + x_gap

        for node, pos in positions.items():
            dpg.set_item_pos(node.id, pos)

    def link(self, sender, app_data):
        edge_id = dpg.add_node_link(app_data[0], app_data[1], parent=self.node_editor)
        input: Node = self.graph.node_lookup_by_attribute_id[app_data[0]]
        output: Node = self.graph.node_lookup_by_attribute_id[app_data[1]]

        edge = EdgeGui(edge_id, None, input, output, app_data[0], app_data[1])
        self.graph.link(input, output, edge)

    def delink(self, sender, app_data):
        edge: EdgeGui = self.graph.edge_lookup_by_edge_id[app_data]
        edge.disconnect()
        self.graph.adjacency_list[edge.input].remove(edge.output)
        self.graph.edge_lookup_by_edge_id.pop(edge.id)

    def delete_node(self, node):
        incoming = [e for edges in node.input_attributes.values() for e in edges]
        outgoing = [e for edges in node.output_attributes.values() for e in edges]

        # If exactly one input and one output, remember the nodes for reconnection
        reconnect = None
        if len(incoming) == 1 and len(outgoing) == 1:
            a = incoming[0].input  # Upstream node
            c = outgoing[0].output  # Downstream node
            reconnect = (a, c)

        # Delink all connected edges
        for edge in incoming + outgoing:
            edge_id = edge.id
            if edge_id in self.graph.edge_lookup_by_edge_id:
                self.delink(self.node_editor, edge_id)
                self.graph.edge_lookup_by_edge_id.pop(edge_id, None)

        # Reconnect A -> C if valid
        if reconnect:
            a, c = reconnect
            if a.output_attributes and c.input_attributes and a is not c:
                self.link(
                    self.node_editor,
                    (next(iter(a.output_attributes)), next(iter(c.input_attributes))),
                )

        self.graph.adjacency_list.pop(node, None)
        for adj in self.graph.adjacency_list.values():
            if node in adj:
                adj.remove(node)

        for attr_id in itertools.chain(node.input_attributes, node.output_attributes):
            self.graph.node_lookup_by_attribute_id.pop(attr_id, None)

    def save_graph(self):
        with dpg.window(
            label="Name Him, Name Your Son!",
            width=300,
            height=120,
            no_move=True,
            no_resize=True,
            modal=True,
        ) as saviour:
            with dpg.group():
                text = dpg.add_input_text(hint="Name Him...", width=-1)
                dpg.add_button(
                    label="Save",
                    callback=lambda: self.graph.save(dpg.get_value(text)),
                    width=-1,
                )
        dpg.split_frame()
        modal_dimensions = dpg.get_item_rect_size(saviour)
        window_dimensions = dpg.get_item_rect_size("Primary Window")
        newPos = [(window_dimensions[i] - modal_dimensions[i]) / 2 for i in range(2)]
        dpg.configure_item(saviour, pos=newPos)

    def load_graph_window(self):
        with dpg.window(
            label="Load Workflow",
            modal=True,
            tag="Workflows",
            width=380,
            height=440,
            no_resize=True,
            no_move=True,
            on_close=lambda: dpg.delete_item("Workflows"),
        ):
            dpg.add_input_text(
                hint="Search workflows...",
                width=-1,
                callback=lambda s, a: dpg.set_value(filter, a),
            )
            with dpg.child_window():
                filter = dpg.add_filter_set()
            for file in Path("./Data/Workflows/").iterdir():
                with dpg.group(horizontal=True, filter_key=file.name, parent=filter):
                    dpg.add_button(
                        label=file.name,
                        width=-1,
                        callback=lambda: self.load_graph(file.name),
                    )

        dpg.split_frame()
        modal_size = dpg.get_item_rect_size("Workflows")
        win_size = dpg.get_item_rect_size("Primary Window")

        dpg.configure_item(
            "Workflows",
            pos=[(win_size[i] - modal_size[i]) / 2 for i in range(2)],
        )

    def load_graph(self, filename):
        for node in self.graph.load_nodes(filename, visual_mode=True):
            node.init_dpg(self.node_editor, self.graph.evaluate)
            node.setup_attributes()
            self.add_node(node)
        for edges in self.graph.load_node_output_attributes(filename, visual_mode=True):
            for edge in edges:
                input_node = self.graph.node_lookup_by_uuid[edge["input"]]
                output_node = self.graph.node_lookup_by_uuid[edge["output"]]
                input_attr = input_node.output_attributes_id_lookup[edge["input_attr"]]
                output_attr = output_node.input_attributes_id_lookup[
                    edge["output_attr"]
                ]
                self.link(self.node_editor, (input_attr, output_attr))
        self.auto_arrange()

    def add_node(self, node: Node):
        delete_hook = functools.partial(self.delete_node, node)
        node.delete_hook = delete_hook
        self.graph.add_node(node)

    def add_rgb_splitter_node(self):
        node = Nodes.RGBSplitter()
        node.init_dpg(parent=self.node_editor, update_hook=self.graph.evaluate)
        node.setup_attributes()
        self.add_node(node)

    def add_merge_node(self):
        node = Nodes.Merge()
        node.init_dpg(parent=self.node_editor, update_hook=self.graph.evaluate)
        node.setup_attributes()
        self.add_node(node)

    def add_smh_splitter_node(self):
        node = Nodes.SMHSplitter()
        node.init_dpg(parent=self.node_editor, update_hook=self.graph.evaluate)
        node.setup_attributes()
        self.add_node(node)

    def add_histogram_node(self):
        node = Nodes.HistogramNode()
        node.init_dpg(parent=self.node_editor, update_hook=self.graph.evaluate)
        node.setup_attributes()
        self.add_node(node)

    def add_preview_node(self):
        node = Nodes.PreviewNode()
        node.init_dpg(parent=self.node_editor, update_hook=self.graph.evaluate)
        node.setup_attributes()
        self.add_node(node)

    def add_image_node(self):
        node = Nodes.ImageNode(self.image)
        node.init_dpg(
            parent=self.node_editor,
            update_hook=self.graph.evaluate,
        )
        node.setup_attributes()
        self.add_node(node)

    def add_saturation_node(self):
        node = Nodes.Saturation()
        node.init_dpg(parent=self.node_editor, update_hook=self.graph.evaluate)
        node.setup_attributes()
        self.add_node(node)

    def add_contrast_node(self):
        node = Nodes.Contrast()
        node.init_dpg(parent=self.node_editor, update_hook=self.graph.evaluate)
        node.setup_attributes()
        self.add_node(node)

    def add_brightness_node(self):
        node = Nodes.Brightness()
        node.init_dpg(parent=self.node_editor, update_hook=self.graph.evaluate)
        node.setup_attributes()
        self.add_node(node)

    def add_colour_balance_node(self):
        node = Nodes.ColourBalance()
        node.init_dpg(
            parent=self.node_editor,
            update_hook=self.graph.evaluate,
        )
        node.setup_attributes()
        self.add_node(node)

    def add_levels_node(self):
        node = Nodes.Levels()
        node.init_dpg(
            parent=self.node_editor,
            update_hook=self.graph.evaluate,
        )
        node.setup_attributes()
        self.add_node(node)
