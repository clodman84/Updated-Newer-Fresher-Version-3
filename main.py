import logging
from pathlib import Path

import dearpygui.dearpygui as dpg

import GUI
from Application import ImageManager
from Application.images import Image
from GUI.bill import BillingWindow


def main():
    dpg.create_context()
    dpg.create_viewport(title="DoPy")
    core_logger = logging.getLogger("Core")
    gui_logger = logging.getLogger("GUI")
    core_logger.setLevel(logging.DEBUG)
    gui_logger.setLevel(logging.DEBUG)
    formatter = logging.Formatter(
        "[{asctime}] [{levelname:<8}] {name}: {message}", "%H:%M:%S", style="{"
    )
    with dpg.window(tag="Primary Window"):
        with dpg.menu_bar():
            with dpg.menu(label="Tools"):
                dpg.add_menu_item(
                    label="Show Item Registry", callback=dpg.show_item_registry
                )
                dpg.add_menu_item(
                    label="Show Performance Metrics", callback=dpg.show_metrics
                )
                dpg.add_menu_item(label="Show Debug", callback=dpg.show_debug)
            with dpg.window(height=350, width=350, label="Logger") as logger_window:
                log = GUI.Logger(parent=logger_window)
                log.setFormatter(formatter)
                core_logger.addHandler(log)
                gui_logger.addHandler(log)

    with dpg.window() as image_window:
        demo_roll = Path("./30R")
        image_manager = ImageManager("offline", "Sugar Mommy", "30R", path=demo_roll)
        billing_window = BillingWindow(cam="Sugar Mommy", roll="30R")
        GUI.ImageWindow(image_window, billing_window, image_manager)

    dpg.setup_dearpygui()
    dpg.set_primary_window("Primary Window", True)
    dpg.set_viewport_vsync(False)
    dpg.show_viewport(maximized=True)
    dpg.start_dearpygui()
    dpg.destroy_context()


if __name__ == "__main__":
    main()
