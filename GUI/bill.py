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
    def __init__(
        self,
        roll: str,  # this is how we decide what autosave file we need to open
        source: list[Path],  # it's your job to give the billingwindow images to bill
        parent: str | int,
    ):
        # List of things the BilledWindow knows about:
        # 1. The roll that it is responsible for

        # What does the BilledWindow do?
        # 1. Writes the ID and details of all the students into the database (autosave)
        # 2. Advances the billing process (loads the appropriate image)

        self.roll = roll
        self.num_images = len(source)
        self.ids_per_roll = load(roll) or [
            collections.Counter() for _ in range(self.num_images)
        ]
        self.search_machine = SearchMachine()
        self.current_index = 0
        self.num_rows = 45
        self.total_snaps = 0
        self.source = source
        self.parent = parent

        with dpg.child_window(
            width=400,
            height=-1,
            label=f"Billing Window {self.roll}",
            parent=self.parent,
        ) as self.window:
            with dpg.group(horizontal=True):
                input = dpg.add_input_text(width=150)
                dpg.add_button(label="Same As:", callback=self.same_as)
                self.same_as_input = dpg.add_input_int(default_value=30, width=93)
                dpg.add_button(label="Export", callback=self.export)

            with dpg.group(horizontal=False) as parent:
                self.suggestions_panel = dpg.add_child_window(
                    width=400, height=300, parent=parent
                )
                self.suggestion_table = TableManager9000(
                    parent=self.suggestions_panel,
                    rows=self.num_rows,
                    headers=["Name", "ID", "BWN", "Room"],
                )
                self.suggestion_table["Name"] = dpg.add_text, {"label": ""}
                self.suggestion_table["ID"] = dpg.add_button, {
                    "label": "",
                    "show": False,
                    "callback": lambda s, a, u: self.add_id(u),
                }
                self.suggestion_table["BWN"] = dpg.add_text, {"label": ""}
                self.suggestion_table["Room"] = dpg.add_text, {"label": ""}
                self.suggestion_table.construct()

                for row in range(self.num_rows):
                    # TODO: when getitem is written this should iterate through all the cells of a column nicely
                    with dpg.popup(
                        f"{self.suggestions_panel}_{row}_0",
                        tag=f"{self.suggestions_panel}_{row}_popup",
                    ):
                        dpg.add_input_text(
                            tag=f"{self.suggestions_panel}_{row}_nick_text"
                        )
                        dpg.add_text(
                            "Does this dude have too many fucking pictures?",
                            wrap=200,
                        )
                        dpg.add_separator()
                        with dpg.group(horizontal=True):
                            dpg.add_text("Set a nickname")
                            dpg.add_button(
                                label="Confirm",
                                tag=f"{self.suggestions_panel}_{row}_nick_button",
                                callback=lambda s, a, u: self.set_nick(u),
                                user_data=row,
                            )

                with dpg.child_window(width=400) as billed_panel:
                    self.billed_count = dpg.add_text("0 people billed")
                    self.billed_table = TableManager9000(
                        parent=billed_panel,
                        rows=self.num_rows,  # TODO: maybe this number should be different or TableManager9000 should be more flexible,
                        headers=["ID", "Count", "Name"],
                    )
                    self.billed_table["Name"] = dpg.add_text, {"label": ""}
                    self.billed_table["ID"] = dpg.add_text, {"label": ""}
                    self.billed_table["Count"] = dpg.add_input_int, {
                        "callback": lambda s, a, u: self.set_id(u, a),
                        "default_value": 0,
                        "width": 80,
                    }
                    self.billed_table.construct()
            dpg.set_item_callback(input, self.suggest)
        self.show_selected_ids()

    def export(self):
        copy_images(self.ids_per_roll, Path("./Data/") / self.roll, self.source)
        modal_message(
            "Roll Exported!\nBilled Snaps = {}".format(self.update_total_snaps()),
            checkbox=False,
        )

    def suggest(self, sender, app_data, user_data):
        if len(app_data) > 0:
            matches = self.search_machine.search(app_data)
        else:
            return

        if not matches:
            for row in range(self.num_rows):
                for column, cell in self.suggestion_table[row].items():
                    if column == "ID":
                        dpg.hide_item(cell)
                    dpg.set_value(cell, "")
            dpg.set_value(self.suggestion_table[0]["Name"], "No matches")
        else:
            for row, item in enumerate(matches):
                if row == self.num_rows - 1:
                    break
                for j, column in enumerate(self.suggestion_table.headers):
                    if column == "ID":
                        dpg.set_item_label(
                            self.suggestion_table[row][column], item.idno
                        )
                        dpg.set_item_user_data(
                            self.suggestion_table[row][column], item.idno
                        )
                        dpg.show_item(self.suggestion_table[row][column])

                        # updating the nick name text box
                        (nick,) = db.get_nick(item.idno)
                        nick = nick if nick else ""  # :vomit emoji:
                        dpg.set_value(f"{self.suggestions_panel}_{row}_nick_text", nick)

                    dpg.set_value(self.suggestion_table[row][column], item[j])

            for row in range(len(matches), self.num_rows):
                for column in self.suggestion_table.headers:
                    if column == "ID":
                        dpg.hide_item(self.suggestion_table[row][column])
                    dpg.set_value(self.suggestion_table[row][column], "")

    def set_id(self, id, value):
        self.ids_per_roll[self.current_index][id] = value
        if value == 0:
            self.ids_per_roll[self.current_index].pop(id)
        with SimpleTimer("Autosaved") as timer:
            write(self.ids_per_roll, self.roll)
        logger.debug(timer)
        self.show_selected_ids()

    def add_id(self, id):
        new_val = self.ids_per_roll[self.current_index][id] + 1
        self.set_id(id, new_val)

    def set_nick(self, row):
        nick = dpg.get_value(f"{self.suggestions_panel}_{row}_nick_text")
        id = dpg.get_item_user_data(f"{self.suggestions_panel}_{row}_1")
        if len(nick) < 3:
            logger.debug("Can't set shit")  # nice
            if not dpg.does_item_exist("invalid_nick"):
                with dpg.window(
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
                logger.debug("Close the warning window first da")
        else:
            if not dpg.does_item_exist("invalid_nick"):
                db.set_nick(nick, id)
                dpg.hide_item(f"{self.suggestions_panel}_{row}_popup")
                logger.debug("Nickname has been set")
            else:
                logger.debug("Close the warning window first da")

    def show_selected_ids(self):
        counter = self.ids_per_roll[self.current_index]
        total_billed = sum(self.ids_per_roll[self.current_index].values())
        dpg.set_value(
            self.billed_count,
            f"{total_billed} {'people' if total_billed != 1 else 'person'} billed",
        )

        for row, id in enumerate(counter):
            if row == self.num_rows - 1:
                break
            for column in self.billed_table.headers:
                if column == "Count":
                    dpg.set_item_user_data(self.billed_table[row][column], id)
                    dpg.set_value(self.billed_table[row][column], counter[id])
                    dpg.show_item(self.billed_table[row][column])
                elif column == "ID":
                    dpg.set_value(self.billed_table[row][column], id)
                    dpg.show_item(self.billed_table[row][column])
                elif column == "Name":
                    (name,) = db.get_name(id)
                    dpg.set_value(self.billed_table[row][column], name)
                    dpg.show_item(self.billed_table[row][column])

        for row in range(len(counter), self.num_rows):
            for column in self.billed_table.headers:
                dpg.hide_item(self.billed_table[row][column])

    def load(self, index: int):
        self.current_index = index
        same_as_default = index if index != 0 else self.num_images
        dpg.set_value(self.same_as_input, same_as_default)
        self.show_selected_ids()

    def same_as(self, index):
        index = dpg.get_value(self.same_as_input)
        index -= 1
        for key, value in self.ids_per_roll[index].items():
            self.ids_per_roll[self.current_index][key] = value
        with SimpleTimer("Autosaved") as timer:
            write(self.ids_per_roll, self.roll)
        logger.debug(timer)
        self.show_selected_ids()

    def close(self):
        dpg.delete_item(self.window)

    def update_total_snaps(self):
        self.total_snaps = 0
        for i in self.ids_per_roll:
            for j in i.values():
                self.total_snaps += j
        return self.total_snaps


def show_all_nicks():
    nicks = db.get_all_nicks()
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
            for i in range(len(nicks)):
                with dpg.table_row():
                    for j in range(len(headers_2)):
                        dpg.add_text(nicks[i][j])
