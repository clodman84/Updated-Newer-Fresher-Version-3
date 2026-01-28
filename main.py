import csv
import logging
from collections import Counter
from pathlib import Path

import dearpygui.dearpygui as dpg
from screeninfo import get_monitors

import Application
import GUI
from GUI.fuckups import Genie
from themes import create_gruvbox_dark_theme

logger = logging.getLogger("Core.Main")

DETECT_FACES = True


def toggle_detect_faces():
    global DETECT_FACES
    DETECT_FACES = not DETECT_FACES
    logger.info(f"Face detection set to {DETECT_FACES}")


def setup_db():
    """
    Creates the sqlite database based on the schema in Application/schema.sql
    """
    with open(Path("Data/schema.sql")) as file:
        query = "".join(file.readlines())
    connection = Application.db.connect()
    connection.executescript(query)
    connection.close()


def start_billing(path: Path):
    main_image_ratios = (0.55, 0.65)
    window_ratios = (0.76, 0.79)
    monitors = get_monitors()
    logger.debug(monitors)
    for monitor in monitors:
        main_image_dimensions = tuple(
            int(j * i)
            for i, j in zip(main_image_ratios, (monitor.width, monitor.height))
        )
        thumnail_dimensions = (200, 200)
        window_dimensions = tuple(
            int(j * i) for i, j in zip(window_ratios, (monitor.width, monitor.height))
        )
        logger.debug("Making ImageManager")
        image_manager = Application.ImageManager.from_path(
            path, main_image_dimensions, thumnail_dimensions
        )
        logger.debug("Made ImageManager")
        GUI.ImageWindow(
            roll=path.name,
            detect_faces=DETECT_FACES,
            image_manager=image_manager,
            main_image_dimensions=main_image_dimensions,
            thumnail_dimensions=thumnail_dimensions,
            window_dimensions=window_dimensions,
        )


def load_image_folder(sender, app_data, user_data):
    path = Path(app_data["file_path_name"])

    logger.debug("Called Start Billing")
    start_billing(path)
    logger.debug("Starting Billing")


def load_mess_list(sender, app_data, user_data):
    # yuck
    path = next(iter(app_data["selections"].values()))
    path = Path(path)
    Application.db.read_mess_list(path)
    GUI.modal_message("Mess list loaded successfully!")


def generate_bill(sender, app_data, user_data):
    path = Path(app_data["file_path_name"])
    bill = Application.generate_bill(path, Counter())
    with open("./Data/bill.csv", "w", newline="") as csvfile:
        csv_writer = csv.writer(csvfile)
        csv_writer.writerow(["Item", "Count"])
        for item, count in bill.items():
            id, name, hoscode, roomno = Application.db.get_all_info(item)
            csv_writer.writerow([id, name, hoscode, roomno, count])
    GUI.modal_message("Bill Generated, check the data folder for a CSV!")


def main():
    setup_db()
    dpg.create_context()
    create_gruvbox_dark_theme()
    dpg.create_viewport(title="DoPy")
    dpg.set_viewport_small_icon("./dopylogofinal.ico")
    dpg.set_viewport_large_icon("./dopylogofinal.ico")
    core_logger = logging.getLogger("Core")
    gui_logger = logging.getLogger("GUI")
    core_logger.setLevel(logging.DEBUG)
    gui_logger.setLevel(logging.DEBUG)
    formatter = logging.Formatter(
        "[{threadName}][{asctime}] [{levelname:<8}] {name}: {message}",
        "%H:%M:%S",
        style="{",
    )

    with dpg.colormap_registry():
        dpg.add_colormap(
            [[0, 255, 255], [255, 0, 0]],
            False,
            tag="red",
        )

        dpg.add_colormap(
            [[255, 0, 255], [0, 255, 0]],
            False,
            tag="green",
        )

        dpg.add_colormap(
            [[255, 255, 0], [0, 0, 255]],
            False,
            tag="blue",
        )

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

        dpg.add_file_dialog(
            directory_selector=True,
            show=False,
            tag="crc_bill_dialog",
            callback=generate_bill,
            height=400,
        )

        with dpg.menu_bar():
            with dpg.menu(label="File"):
                dpg.add_menu_item(
                    label="Load Roll",
                    callback=lambda: dpg.show_item("roll_folder_dialog"),
                )
                dpg.add_menu_item(
                    label="Load Mess List",
                    callback=lambda: dpg.show_item("mess_list_file_dialog"),
                )
                dpg.add_menu_item(
                    label="Generate Bill",
                    callback=lambda: dpg.show_item("crc_bill_dialog"),
                )

            with dpg.menu(label="Tools"):
                dpg.add_menu_item(label="Genie", callback=lambda: Genie())
                dpg.add_menu_item(label="Show Nicknames", callback=GUI.show_all_nicks)
                dpg.add_menu_item(
                    label="Show Performance Metrics", callback=dpg.show_metrics
                )
                dpg.add_menu_item(
                    label="Toggle Face Detection", callback=toggle_detect_faces
                )

            with dpg.menu(label="Music"):
                dpg.add_menu_item(
                    label="Play Visualizer",
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

    import sys

    sys.stderr = log

    if sys.platform == "win32":
        from ctypes import windll
        import pywinstyles

        hwnd = windll.user32.FindWindowW(None, "DoPy")
        pywinstyles.change_header_color(hwnd, color="black")

    dpg.start_dearpygui()
    dpg.destroy_context()


if __name__ == "__main__":
    main()
