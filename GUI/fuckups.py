import collections
import logging
from pathlib import Path

import dearpygui.dearpygui as dpg
from screeninfo import get_monitors

from Application import rename_rolls, write
from GUI.utils import modal_message

from .image import ImageManager, ImageWindow

logger = logging.getLogger("GUI.Fuckups")


def rename_images(sender, app_data, user_data):
    logger.debug(app_data)
    path = Path(app_data["file_path_name"])
    still_fucked = rename_rolls(path, [])
    logger.debug(f"{len(still_fucked)} images still fucked")
    dpg.delete_item(sender)
    dpg.delete_item(user_data)

    if still_fucked:
        main_image_ratios = (0.55, 0.65)
        thumnail_ratios = (0.18, 0.32)
        window_ratios = (0.76, 0.79)
        monitors = get_monitors()
        for monitor in monitors:
            if monitor.is_primary:
                main_image_dimensions = tuple(
                    int(j * i)
                    for i, j in zip(main_image_ratios, (monitor.width, monitor.height))
                )
                thumbnail_dimensions = tuple(
                    int(j * i)
                    for i, j in zip(thumnail_ratios, (monitor.width, monitor.height))
                )
                window_dimensions = tuple(
                    int(j * i)
                    for i, j in zip(window_ratios, (monitor.width, monitor.height))
                )

                ids_per_image = [
                    collections.Counter() for _ in range(len(still_fucked))
                ]
                for i, shit in enumerate(still_fucked):
                    for id in shit[1]:
                        ids_per_image[i][id] = 1
                write(ids_per_image, "rename_recovery")

                manager = ImageManager.from_file_list(
                    [i[0] for i in still_fucked],
                    main_image_dimensions,
                    thumbnail_dimensions,
                )
                ImageWindow(
                    "rename_recovery",
                    False,
                    manager,
                    main_image_dimensions,
                    thumbnail_dimensions,
                    window_dimensions,
                )

    modal_message(
        f"All images in {path} have been copied into rename_recovery with the right name, the PS images have not been deleted, if you're confident, delete them with a script"
    )


class Genie:
    def __init__(self):
        with dpg.window(label="Genie") as self.window:
            with dpg.group(tag=f"{self.window}_starter"):
                dpg.add_text("What did you do this time?")
                dpg.add_button(
                    label="My stupid ass billed with the wrong mess list",
                    callback=self.rename_rolls,
                )
                dpg.add_button(
                    label="My stupid ass lost a billed roll",
                    callback=lambda s, a, u: modal_message(
                        "Sorry dawg I can't help you (yet)"
                    ),
                )
                dpg.add_button(
                    label="SD card got corrupted",
                    callback=lambda s, a, u: modal_message(
                        "Sorry dawg I can't help you (yet)"
                    ),
                )

    def rename_rolls(self):
        dpg.hide_item(f"{self.window}_starter")
        button = dpg.add_text(
            "First make sure you've loaded the new mess list", parent=self.window
        )
        dpg.add_file_dialog(
            label="Select the directory with all the roll folders",
            directory_selector=True,
            show=False,
            tag=f"{self.window}_all_rolls_to_rename_dialog",
            user_data=self.window,
            callback=rename_images,
            height=400,
        )

        def _ligma(s, a, u):
            dpg.hide_item(button)
            dpg.add_text(
                "Now select the directory that contains all the roll folders. If you can't find it, through the file dialog just copy the address and paste it in. Keep an eye on the logs, don't mess this up retard",
                parent=self.window,
                wrap=400,
            )
            dpg.show_item(f"{self.window}_all_rolls_to_rename_dialog")
            dpg.hide_item(s)

        dpg.add_button(
            label="Yes I've done it, and I know what I'm doing",
            callback=_ligma,
            parent=self.window,
        )
