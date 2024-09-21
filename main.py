import logging
from pathlib import Path

import dearpygui.dearpygui as dpg
from dearpygui import demo
from screeninfo import get_monitors

import Application
import GUI

logger = logging.getLogger("Core.Main")


def setup_db():
    """
    Creates the sqlite database based on the schema in Application/schema.sql
    """
    with open(Path("Data/schema.sql")) as file:
        query = "".join(file.readlines())
    connection = Application.db.connect()
    connection.executescript(query)
    connection.close()


def make_image_window(path: Path):
    GUI.ImageWindow(path)


def load_image_folder(sender, app_data, user_data):
    path = Path(app_data["file_path_name"])
    make_image_window(path)


def load_mess_list(sender, app_data, user_data):
    # yuck
    path = next(iter(app_data["selections"].values()))
    path = Path(path)
    Application.db.read_mess_list(path)
    GUI.modal_message("Mess list loaded successfully!")


def main():
    setup_db()
    dpg.create_context()
    dpg.create_viewport(title="DoPy")
    core_logger = logging.getLogger("Core")
    gui_logger = logging.getLogger("GUI")
    core_logger.setLevel(logging.DEBUG)
    gui_logger.setLevel(logging.DEBUG)
    formatter = logging.Formatter(
        "[{threadName}][{asctime}] [{levelname:<8}] {name}: {message}",
        "%H:%M:%S",
        style="{",
    )

    monitors = get_monitors()
    for monitor in monitors:
        if monitor.is_primary and monitor.width > 1366:
            dpg.set_global_font_scale(2.5)

    with dpg.window(tag="Primary Window"):
        with dpg.file_dialog(
            directory_selector=False,
            show=False,
            tag="mess_list_file_dialog",
            callback=load_mess_list,
            height=400,
        ):
            dpg.add_file_extension(".csv", color=(0, 255, 0, 255), custom_text="[CSV]")

        dpg.add_file_dialog(
            directory_selector=True,
            show=False,
            tag="roll_folder_dialog",
            callback=load_image_folder,
            height=400,
        )

        with dpg.menu_bar():
            with dpg.menu(label="Tools"):
                dpg.add_menu_item(
                    label="Load Roll",
                    callback=lambda: dpg.show_item("roll_folder_dialog"),
                )
                dpg.add_menu_item(
                    label="Load Mess List",
                    callback=lambda: dpg.show_item("mess_list_file_dialog"),
                )
                dpg.add_menu_item(label="Show Nicknames", callback=GUI.show_all_nicks)
                dpg.add_menu_item(
                    label="Show Performance Metrics", callback=dpg.show_metrics
                )
                dpg.add_menu_item(
                    label="Logger Stress Test", callback=GUI.logger_stress_test
                )
            with dpg.menu(label="Dev"):
                dpg.add_menu_item(
                    label="Spawn Billing Window",
                    callback=lambda: GUI.BillingWindow(
                        roll="Dev", path=Path("Data/Dev")
                    ),
                )
                dpg.add_menu_item(label="Show GUI Demo", callback=demo.show_demo)
            dpg.add_button(
                label="Music",
                callback=lambda: GUI.MusicVisualiser(
                    "./Data/Audio/clodman.mp3"
                ).start(),
            )

    log = GUI.Logger()
    log.setFormatter(formatter)
    core_logger.addHandler(log)
    gui_logger.addHandler(log)

    dpg.setup_dearpygui()
    dpg.set_primary_window("Primary Window", True)
    dpg.set_viewport_vsync(False)
    dpg.show_viewport(maximized=True)
    dpg.start_dearpygui()
    dpg.destroy_context()


if __name__ == "__main__":
    main()
