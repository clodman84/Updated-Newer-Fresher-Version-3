import dearpygui.dearpygui as dpg


class AutocompletePopup:
    def __init__(self):
        pass


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

    def clear(self):
        pass

    def load(self, index: int):
        self.clear()

    def save(self):
        pass
