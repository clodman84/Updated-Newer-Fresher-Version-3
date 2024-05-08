import logging

import dearpygui.dearpygui as dpg

from Application import DJ

logger = logging.getLogger("GUI.Visualiser")

dpg.create_context()


class MusicVisualiser:
    def __init__(self, playlist=[]):
        # List of things the image window knows about:
        # 1. The roll that is currently being billed
        # 2. The images in the roll
        # 3. A BilledWindow

        # What does the ImageWindow do?
        # 1. Creates and manages the BilledWindow
        # 2. Lets us open our image of choice
        # 3. Has a preview for the next and previous image
        self.dj = DJ(self.visualiser)
        self.playlist = playlist
        with dpg.window(label="DJ", no_resize=True):
            dpg.add_simple_plot(
                default_value=list(range(32)),
                histogram=True,
                tag="bar_series",
                width=200,
                height=50,
            )
            with dpg.group(horizontal=True):
                dpg.add_button(label="Stop", callback=self.stop)
                dpg.add_button(label="Start", callback=self.start)

    def visualiser(self, fft: list):
        # relative power spectrum
        dpg.set_value("bar_series", fft)

    def start(self):
        for track in self.playlist:
            self.dj.play(track)

    def stop(self):
        self.dj.stop()
