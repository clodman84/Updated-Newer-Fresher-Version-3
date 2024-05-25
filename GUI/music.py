import logging

import dearpygui.dearpygui as dpg

from Application import DJ

logger = logging.getLogger("GUI.Visualiser")

dpg.create_context()


class MusicVisualiser:
    """UI representation of the DJ object. The DJ can live without this, but the visualiser can't live without
    the DJ :-("""

    def __init__(self, track):
        self.dj = DJ(self.visualiser)
        self.track = track
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
                self.hidden_button = dpg.add_button(
                    label="Change", callback=self.alternate, show=False
                )

    def alternate(self):
        # fuck it this is my easter egg
        self.stop()
        if self.track == "./Data/Audio/clodman.mp3":
            self.track = "Data/Audio/clodman_alternate.mp3"
        else:
            self.track = "./Data/Audio/clodman.mp3"
        self.start()

    def visualiser(self, fft: list):
        # relative power spectrum
        dpg.set_value("bar_series", fft)

    def start(self):
        self.dj.play(self.track)
        if "clodman" in self.track:
            dpg.show_item(self.hidden_button)
        else:
            dpg.hide_item(self.hidden_button)

    def stop(self):
        self.dj.stop()
