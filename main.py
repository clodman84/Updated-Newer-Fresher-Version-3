import logging
from pathlib import Path

import dearpygui.dearpygui as dpg
from dearpygui import demo

import Application
import GUI


def make_image_window():
    with dpg.window(width=1035, height=608) as image_window:
        demo_roll = Path("./30R")
        image_manager = Application.ImageManager(
            "offline", "Sugar Mommy", "30R", path=demo_roll
        )
        billing_window = GUI.BillingWindow(cam="Sugar Mommy", roll="30R")
        GUI.ImageWindow(image_window, billing_window, image_manager)


def load_mess_list(sender, app_data, user_data):
    # yuck
    path = next(iter(app_data["selections"].values()))
    path = Path(path)
    Application.db.read_mess_list(path)
    GUI.modal_message("Mess list loaded successfully!")


def main():
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
    with dpg.window(tag="Primary Window"):
        with dpg.file_dialog(
            directory_selector=False,
            show=False,
            tag="mess_list_file_dialog",
            callback=load_mess_list,
        ):
            # disgusting
            dpg.add_file_extension(".*")
            dpg.add_file_extension("", color=(150, 255, 150, 255))
            dpg.add_file_extension(".csv", color=(0, 255, 0, 255), custom_text="[CSV]")

        with dpg.menu_bar():
            with dpg.menu(label="Tools"):
                dpg.add_menu_item(
                    label="Show Item Registry", callback=dpg.show_item_registry
                )
                dpg.add_menu_item(
                    label="Show Performance Metrics", callback=dpg.show_metrics
                )
                dpg.add_menu_item(label="Show Debug", callback=dpg.show_debug)
                dpg.add_menu_item(
                    label="Load Mess List",
                    callback=lambda: dpg.show_item("mess_list_file_dialog"),
                )
            dpg.add_button(label="Open ImageViewer", callback=make_image_window)
            dpg.add_button(label="Open Demo", callback=demo.show_demo)
            dpg.add_button(
                label="Music",
                callback=lambda: GUI.MusicVisualiser(
                    "./Data/Audio/clodman.mp3"
                ).start(),
            )
    with dpg.window(height=350, width=350, label="Logger") as logger_window:
        log = GUI.Logger(parent=logger_window)
        log.setFormatter(formatter)
        core_logger.addHandler(log)
        gui_logger.addHandler(log)
    GUI.BillingWindow(cam="Sugar Mommy", roll="30R")
    dpg.setup_dearpygui()
    dpg.set_primary_window("Primary Window", True)
    dpg.set_viewport_vsync(False)
    dpg.show_viewport(maximized=True)
    dpg.start_dearpygui()
    dpg.destroy_context()


if __name__ == "__main__":
    main()
