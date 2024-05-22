import dearpygui.dearpygui as dpg

from Application import search


class SuggestionPanel:
    def __init__(self, parent):
        self.parent = parent
        for i in range(15):
            dpg.add_text("Lmao", tag=f"{self.parent}_{i}", parent=parent)

    def update(self, sender, app_data, user_data):
        if len(app_data) > 0:
            matches = search(app_data)
        else:
            return

        if not matches:
            dpg.set_value(f"{self.parent}_{0}", "No matches")
            for i in range(1, 15):
                dpg.set_value(f"{self.parent}_{i}", "")
        else:
            for i in range(len(matches)):
                dpg.set_value(f"{self.parent}_{i}", matches[i][0])
            for i in range(len(matches), 15):
                dpg.set_value(f"{self.parent}_{i}", "")


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
        with dpg.window(width=500, label="Billing Window"):
            with dpg.group(horizontal=True, width=0):
                with dpg.child_window(width=250) as suggestions:
                    self.suggestions = SuggestionPanel(suggestions)
                with dpg.child_window(width=250) as self.window:
                    dpg.add_input_text(callback=self.suggestions.update)

    def clear(self):
        pass

    def load(self, index: int):
        self.clear()

    def save(self):
        pass
