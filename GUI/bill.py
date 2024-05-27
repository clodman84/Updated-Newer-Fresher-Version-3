import collections
import logging

import dearpygui.dearpygui as dpg

from Application import SearchMachine, load, write
from Application.utils import SimpleTimer

logger = logging.getLogger("GUI.Bill")


class BillingWindow:
    """Tags and stores images. There is no real ImageManager equivalent for BillingWindow, so this does everything"""

    def __init__(self, cam: str, roll: str):
        # List of things the BilledWindow knows about:
        # 1. The cam and roll that it is responsible for

        # What does the BilledWindow do?
        # 1. Writes the ID and details of all the students into the database (autosave)
        # 2. Advances the billing process (loads the appropriate image)

        self.roll = roll
        self.cam = cam
        self.ids_per_roll = load(cam, roll) or [
            collections.Counter() for _ in range(40)
        ]
        self.search_machine = SearchMachine()
        self.current_index = 0

        with dpg.window(width=625, height=436, label="Billing Window", no_resize=True):
            input = dpg.add_input_text()
            with dpg.group(horizontal=True):
                with dpg.child_window(width=350) as self.suggestions_panel:
                    with dpg.table(policy=dpg.mvTable_SizingFixedFit):
                        dpg.add_table_column(label="Name")
                        dpg.add_table_column(label="ID")
                        dpg.add_table_column(label="Bhawan", width=50)
                        dpg.add_table_column(label="Room", width=50)

                        for i in range(15):
                            with dpg.table_row():
                                for j in range(4):
                                    if j == 1:
                                        dpg.add_button(
                                            label="",
                                            tag=f"{self.suggestions_panel}_{i}_{j}",
                                            show=False,
                                            callback=lambda s, a, u: self.add_id(u),
                                        )
                                    else:
                                        dpg.add_text(
                                            "", tag=f"{self.suggestions_panel}_{i}_{j}"
                                        )

                with dpg.child_window(width=250):
                    with dpg.table(resizable=True) as self.ids_table:
                        dpg.add_table_column(label="ID")
                        dpg.add_table_column(label="Count")
            dpg.set_item_callback(input, self.suggest)
        self.show_selected_ids()

    def suggest(self, sender, app_data, user_data):
        if len(app_data) > 0:
            matches = self.search_machine.search(app_data)
        else:
            return

        if not matches:
            for i in range(15):
                for j in range(4):
                    if j == 1:
                        dpg.hide_item(f"{self.suggestions_panel}_{i}_{j}")
                    dpg.set_value(f"{self.suggestions_panel}_{i}_{j}", "")

            dpg.set_value(f"{self.suggestions_panel}_{0}_{0}", "No matches")
        else:
            for i, item in enumerate(matches):
                if i == 14:
                    break
                for j in range(4):
                    if j == 1:
                        dpg.set_item_label(f"{self.suggestions_panel}_{i}_{j}", item[j])
                        dpg.set_item_user_data(
                            f"{self.suggestions_panel}_{i}_{j}", item[j]
                        )
                        dpg.show_item(f"{self.suggestions_panel}_{i}_{j}")
                    dpg.set_value(f"{self.suggestions_panel}_{i}_{j}", item[j])
            for i in range(len(matches), 15):
                for j in range(4):
                    if j == 1:
                        dpg.hide_item(f"{self.suggestions_panel}_{i}_{j}")
                    dpg.set_value(f"{self.suggestions_panel}_{i}_{j}", "")

    def set_id(self, id, value):
        self.ids_per_roll[self.current_index][id] = value
        if value == 0:
            self.ids_per_roll[self.current_index].pop(id)
        with SimpleTimer("Autosaved") as timer:
            write(self.ids_per_roll, self.cam, self.roll)
        logger.debug(timer)
        self.show_selected_ids()

    def add_id(self, id):
        new_val = self.ids_per_roll[self.current_index][id] + 1
        self.set_id(id, new_val)

    def show_selected_ids(self):
        counter = self.ids_per_roll[self.current_index]
        self.clear()
        for key, value in counter.items():
            with dpg.table_row(parent=self.ids_table):
                dpg.add_text(key)
                dpg.add_input_int(
                    default_value=value,
                    callback=lambda s, a, u: self.set_id(u, a),
                    user_data=key,
                )

    def clear(self):
        for item in dpg.get_item_children(self.ids_table)[1]:
            dpg.delete_item(item)

    def load(self, index: int):
        self.current_index = index
        self.show_selected_ids()

    def save(self):
        pass
