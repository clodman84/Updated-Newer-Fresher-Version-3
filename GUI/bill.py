import logging

import dearpygui.dearpygui as dpg

from Application import search

logger = logging.getLogger("GUI.Bill")


class SuggestionPanel:
    def __init__(self, parent):
        self.parent = parent
        with dpg.table(policy=dpg.mvTable_SizingFixedFit):
            dpg.add_table_column(label="Name")
            dpg.add_table_column(label="ID")
            dpg.add_table_column(label="Bhawan", width=50)
            dpg.add_table_column(label="Room", width=50)

            for i in range(15):
                with dpg.table_row():
                    for j in range(4):
                        dpg.add_text("", tag=f"{self.parent}_{i}_{j}")

    def update(self, sender, app_data, user_data):
        if len(app_data) > 0:
            matches = search(app_data)
        else:
            return

        if not matches:
            for i in range(15):
                for j in range(4):
                    dpg.set_value(f"{self.parent}_{i}_{j}", "")
            dpg.set_value(f"{self.parent}_{0}_{0}", "No matches")
        else:
            for i in range(len(matches)):
                for j in range(4):
                    dpg.set_value(f"{self.parent}_{i}_{j}", matches[i][j])
            for i in range(len(matches), 15):
                for j in range(4):
                    dpg.set_value(f"{self.parent}_{i}_{j}", "")


class BillingWindow:
    def __init__(self, cam: str, roll: str):
        # List of things the BilledWindow knows about:
        # 1. The cam and roll that it is responsible for

        # What does the BilledWindow do?
        # 1. Writes the ID and details of all the students into the database (autosave)
        # 2. Manages AutocompletePopups as the user starts typing into the text boxes
        # 3. Dynamically resizes with more text boxes if needed
        # 4. Manages the textboxes

        self.roll = roll
        self.cam = cam
        with dpg.window(width=575, height=436, label="Billing Window"):
            input = dpg.add_input_text()
            with dpg.group(horizontal=True):
                with dpg.child_window(width=350) as suggestions:
                    self.suggestions = SuggestionPanel(suggestions)
                with dpg.child_window(width=200) as suggestions:
                    dpg.add_text("WIP")
            dpg.set_item_callback(input, self.suggestions.update)

    def clear(self):
        pass

    def load(self, index: int):
        self.clear()

    def save(self):
        pass
