import functools
import logging
from typing import Callable

import dearpygui.dearpygui as dpg

logger = logging.getLogger("GUI.Tablez")


class TableManager9000:
    def __init__(self, parent, rows: int, headers: list[str]):
        self.num_rows = rows
        placeholder = lambda: None
        self.columns: dict[str, tuple[Callable, dict]] = {
            column_name: (placeholder, {}) for column_name in headers
        }
        self.headers = headers
        self.parent = parent
        self.table = None

    def construct(self):
        """
        Construct can only be called after all the columns have been asigned something to do
        """
        with dpg.table(
            parent=self.parent, policy=dpg.mvTable_SizingFixedFit, scrollX=True
        ) as self.table:
            for name in self.headers:
                dpg.add_table_column(label=name)
            for row in range(self.num_rows):
                with dpg.table_row():
                    for column_number, column_name in enumerate(self.headers):
                        # running usercode to instantiate the cell
                        func, kwargs = self.columns[column_name]
                        # TODO: change this self.table later
                        func(**kwargs, tag=f"{self.parent}_{row}_{column_number}")

    def __setitem__(self, column, task):
        # I know that this is technically slower than assigning all the columns and rows in one big sweep,
        # but fuck you, that was a pain in the ass to make sense of and this will happen only once anyways
        self.columns[column] = task

    def __getitem__(self, key) -> dict[str, str]:
        return {
            column: f"{self.parent}_{key}_{column_n}"
            for column_n, column in enumerate(self.headers)
        }
