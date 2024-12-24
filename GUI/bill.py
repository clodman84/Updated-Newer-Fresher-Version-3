import collections
import logging
from pathlib import Path

import dearpygui.dearpygui as dpg

from Application import SearchMachine, copy_images, db, load, write
from Application.utils import SimpleTimer
from GUI.tablez import TableManager9000
from GUI.utils import modal_message

logger = logging.getLogger("GUI.Bill")


class BillingWindow:
    def __init__(self, roll: str, path: Path, num_images: int):
        # List of things the BilledWindow knows about:
        # 1. The cam and roll that it is responsible for

        # What does the BilledWindow do?
        # 1. Writes the ID and details of all the students into the database (autosave)
        # 2. Advances the billing process (loads the appropriate image)

        self.roll = roll
        self.ids_per_roll = load(roll) or [
            collections.Counter() for _ in range(num_images)
        ]
        logger.debug(self.ids_per_roll)
        self.search_machine = (
            SearchMachine()
        )  # The search machine is in Application/search.py
        self.current_index = 0
        self.path = path
        self.num_rows = 45  # This is the maximum number of rows to be displayed every time you search for a name
        self.total_snaps = 0

        with dpg.window(
            # Sets up the parent window which contains everything
            width=625,
            height=436,
            label=f"Billing Window {self.path.name}",
            no_resize=True,
            no_close=True,
        ) as self.window:
            with dpg.group(horizontal=True):
                # This is the Search Bar, Same As and Export Buttons
                dpg.add_text("Search")
                input = dpg.add_input_text(width=250)
                self.same_as_input = dpg.add_input_int(default_value=30, width=80)
                dpg.add_button(label="Same As", callback=self.same_as)
                dpg.add_button(label="Export", callback=self.export)

            with dpg.group(horizontal=True) as parent:
                self.suggestions_panel = dpg.add_child_window(
                    width=350, parent=parent
                )  # The window for the table being displayed
                self.suggestion_table = TableManager9000(
                    # This is a call to TableManager9000, found in GUI/tablez.py. This is responsible for the table you see.
                    # Say thank you to the original programmer if you see them (if you know who it is, you know).
                    parent=self.suggestions_panel,
                    rows=self.num_rows,
                    headers=[
                        "Name",
                        "ID",
                        "Bhawan",
                        "Room",
                    ],  # The headers will be initialized. At this line, they are just names.
                )
                self.suggestion_table["Name"] = dpg.add_text, {"label": ""}
                self.suggestion_table["ID"] = dpg.add_button, {
                    # The ID is a button
                    "label": "",
                    "show": False,
                    "callback": lambda s, a, u: self.add_id(u),
                }
                self.suggestion_table["Bhawan"] = dpg.add_text, {"label": ""}
                self.suggestion_table["Room"] = dpg.add_text, {"label": ""}
                self.suggestion_table.construct()  # Bob the Builder reference

                for row in range(self.num_rows):
                    # TODO: when getitem is written this should iterate through all the cells of a column nicely
                    with dpg.popup(
                        # Sets up the nickname popup. Activated by right clicks of the mouse.
                        f"{self.suggestions_panel}_{row}_0",
                        tag=f"{self.suggestions_panel}_{row}_popup",
                    ):
                        dpg.add_input_text(
                            # Adds the nickname input bar
                            tag=f"{self.suggestions_panel}_{row}_nick_text"
                        )
                        dpg.add_text(
                            # Adds the text you see. It is wrapped.
                            "Does this dude have too many fucking pictures?",
                            wrap=200,
                        )
                        dpg.add_separator()  # Adds the white line
                        with dpg.group(horizontal=True):
                            # This is the final group you see
                            dpg.add_text("Set a nickname")
                            dpg.add_button(
                                label="Confirm",
                                tag=f"{self.suggestions_panel}_{row}_nick_button",
                                callback=lambda s, a, u: self.set_nick(u),
                                user_data=row,
                            )

                with dpg.child_window(width=250):
                    # This is the child window which keeps track of the IDs and the count
                    with dpg.table(resizable=True) as self.ids_table:
                        dpg.add_table_column(label="ID")
                        dpg.add_table_column(label="Count")
            dpg.set_item_callback(
                input, self.suggest
            )  # Sets the function that will be executed upon searching for a name as the self.suggest() function
        self.show_selected_ids()  # Shows the ID and count saved to the required table on initialization itself

    def export(self):
        """
        Is responsible for exporting the images once billed to the specified path
        """
        copy_images(
            self.ids_per_roll, self.path
        )  # This function exists in Application/store.py
        modal_message(
            # This function exists in GUI/utils.py
            "Roll Exported!\nBilled Snaps = {}".format(self.update_total_snaps()),
            checkbox=False,
        )

    def suggest(self, sender, app_data, user_data):
        """
        The main function which suggests names based on the input text

        Args:
            sender (): Not used here; check the DPG docs (https://dearpygui.readthedocs.io/en/latest/documentation/item-callbacks.html)
            app_data (str): This is what you type in the search bar when searching for a name.
            user_data (): Not used here; check the DPG docs (https://dearpygui.readthedocs.io/en/latest/documentation/item-callbacks.html)

        Returns:

        """
        if len(app_data) > 0:
            matches = self.search_machine.search(
                app_data
            )  # The result of the query (app_data) to the database
        else:
            return

        # Above code searches for the matches only if there is an input

        if not matches:
            # Sets the value of the table to be nothing and adds the text "No matches" when there are no matches
            for row in range(self.num_rows):
                for column, cell in self.suggestion_table[row].items():
                    if column == "ID":
                        dpg.hide_item(cell)  # Hides the button (might break if removed)
                    dpg.set_value(cell, "")  # Sets each cell to be ""
            dpg.set_value(
                self.suggestion_table[0]["Name"], "No matches"
            )  # Modifies the "" to "No matches"
        else:
            for row, item in enumerate(matches):
                if row == self.num_rows - 1:
                    # Breaks if last row
                    break
                for j, column in enumerate(self.suggestion_table.headers):
                    if column == "ID":
                        # Sets the label of the button, with the user_data being the ID supplied to the callback of the button
                        # as the parameter "u" (check line 62 of this file)
                        dpg.set_item_label(
                            self.suggestion_table[row][column], item.idno
                        )
                        dpg.set_item_user_data(
                            self.suggestion_table[row][column], item.idno
                        )
                        dpg.show_item(
                            self.suggestion_table[row][column]
                        )  # Shows the button (by default the button is hidden)

                        # updating the nick name text box
                        (nick,) = db.get_nick(item.idno)
                        nick = nick if nick else ""  # :vomit emoji:
                        dpg.set_value(f"{self.suggestions_panel}_{row}_nick_text", nick)

                    dpg.set_value(
                        self.suggestion_table[row][column], item[j]
                    )  # If the column is not ID, we can directly set value as they are simple text

            for row in range(len(matches), self.num_rows):
                # In case there are less than 45 matches, we iterate over the remaining space
                # hiding the ID buttons and setting the values of each cell as ""
                for column in self.suggestion_table.headers:
                    if column == "ID":
                        dpg.hide_item(self.suggestion_table[row][column])
                    dpg.set_value(self.suggestion_table[row][column], "")

    def set_id(self, id, value):
        """
        Simply updates the counter from the new_val variable in add_id().
        Writes the updated list to the roll and times it.
        Proceeds to call show_selected_ids().

        Args:
            id (str): ID number of the chosen student
            value (int): The number of times student is billed for image

        Returns:

        """
        self.ids_per_roll[self.current_index][
            id
        ] = value  # Updates the Counter with the new value
        if value == 0:
            self.ids_per_roll[self.current_index].pop(id)  # Self explanatory
        with SimpleTimer("Autosaved") as timer:
            # SimpleTimer is present in Application/utils.py
            write(
                self.ids_per_roll, self.roll
            )  # This function is present in Application/store.py
        logger.debug(timer)
        self.show_selected_ids()

    def add_id(self, id):
        """
        Callback to the button on all IDs.
        Gives a new_val variable the value of Counter + 1 and sends it to the set_id() function along with the ID.

        Args:
            id (str): The ID number of the chosen student

        Returns:

        """
        new_val = self.ids_per_roll[self.current_index][id] + 1
        self.set_id(id, new_val)

    def set_nick(self, row):
        """
        Gets a nickname from the inputted text, and sets it if it's length is more than 3.
        If the nickname is less than 3, nickname is invalid and is not set.

        Args:
            row (int): Row of the name which has been right clicked on

        Returns:

        """
        nick = dpg.get_value(f"{self.suggestions_panel}_{row}_nick_text")
        id = dpg.get_item_user_data(f"{self.suggestions_panel}_{row}_1")
        if len(nick) < 3:
            logger.debug("Can't set shit")  # nice
            if not dpg.does_item_exist(
                "invalid_nick"
            ):  # Checks for the non-existence of the warning window
                with dpg.window(
                    # The warning window
                    width=200,
                    height=100,
                    no_close=True,
                    no_resize=True,
                    tag="invalid_nick",
                ):
                    dpg.add_text(
                        "You cannot have a nickname\nbe lesser than 3 letters\ndumbass!"
                    )
                    dpg.add_button(
                        label="yeah yeah",
                        callback=lambda: dpg.delete_item("invalid_nick"),
                    )
            else:
                # In the case that warning window has not been closed, only the message below gets logged.
                logger.debug("Close the warning window first da")
        else:
            if not dpg.does_item_exist(
                "invalid_nick"
            ):  # Checks for the non-existence of the warning window
                db.set_nick(nick, id)
                dpg.hide_item(f"{self.suggestions_panel}_{row}_popup")
                logger.debug("Nickname has been set")
            else:
                logger.debug(
                    "Close the warning window first da"
                )  # Do you like the bangalorian?

    def show_selected_ids(self):
        """
        Displays the IDs and the number of times they are billed for the particular snap.
        This is done in the self.ids_table.
        Called by set_id()
        As a whole it is add_id() [ finds the new value ] -> set_id() [ updates the counter with new value ] <-> show_selected_ids() [ displays the value ].
        """
        counter = self.ids_per_roll[self.current_index]
        self.clear()  # Clears the table first
        for key, value in counter.items():
            with dpg.table_row(parent=self.ids_table):
                dpg.add_text(key)  # This is the ID
                dpg.add_input_int(
                    # This is the count, in the form of a changeable integer
                    default_value=value,
                    callback=lambda s, a, u: self.set_id(
                        u, a
                    ),  # Calls the set_id() function as a callback when updated ( creating a cool loop )
                    user_data=key,
                )

    def clear(self):
        """
        Clears the self.ids_table by deleting items rowwise
        """
        for item in dpg.get_item_children(self.ids_table)[1]:
            dpg.delete_item(item)

    def load(self, index: int):
        """
        Is responsible for updating the current index.
        Also updates the default same as number.
        Called in GUI/image.py.

        Args:
            index (int): the new value of the index

        Returns:

        """
        self.current_index = index
        same_as_default = index if index != 0 else 30
        dpg.set_value(self.same_as_input, same_as_default)
        self.show_selected_ids()

    def save(self):
        """
        Masti sauce?!?!
        """
        pass

    def same_as(self, index):
        """
        Gives functionality to the same as button.
        Goes to the previous index and copies the same IDs and their count onto the current index.
        default previous index is 30.

        Args:
            index (int): The current index.

        Returns:

        """
        index = dpg.get_value(self.same_as_input)
        index -= 1  # Index of the previous image
        for key, value in self.ids_per_roll[index].items():
            self.ids_per_roll[self.current_index][
                key
            ] = value  # IDs in the previous image are being filled
        with SimpleTimer("Autosaved") as timer:
            write(self.ids_per_roll, self.roll)
        logger.debug(timer)
        self.show_selected_ids()

    def close(self):
        """
        Closes the whole window.
        Is called in GUI/image.py to close the billing window along with the image window.
        """
        dpg.delete_item(self.window)

    def update_total_snaps(self):
        """
        Updates the total number of snaps.
        Called at the export() function to display total snaps.
        """
        self.total_snaps = 0
        for i in self.ids_per_roll:
            for j in i.values():
                self.total_snaps += j
        return self.total_snaps


def show_all_nicks():
    """
    Shows all the nicknames
    """
    nicks = db.get_all_nicks()  # Call to the database
    headers_2 = ["Name", "ID", "Nicks"]
    with dpg.window(
        modal=True,
        tag="all_nicks",
        width=500,
        height=300,
        no_resize=True,
        on_close=lambda: dpg.delete_item("all_nicks"),
    ):
        with dpg.table(
            parent="all_nicks",
            scrollX=True,
            row_background=True,
            borders_innerH=True,
            borders_innerV=True,
            borders_outerH=True,
            borders_outerV=True,
        ):
            for name in headers_2:
                dpg.add_table_column(label=name)
            for i in range(len(nicks)):  # i are the total rows
                with dpg.table_row():
                    for j in range(len(headers_2)):  # j are the columns
                        dpg.add_text(nicks[i][j])  # adds the cells individually
