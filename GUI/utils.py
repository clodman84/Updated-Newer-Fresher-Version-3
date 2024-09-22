import logging
import math
import threading

import dearpygui.dearpygui as dpg

from Application.utils import Singleton

MODAL_HIDDEN_LIST = []

logger = logging.getLogger("GUI.Utils")


def modal_message(message, checkbox=True):
    """When you need a popup"""
    print(message)
    if message in MODAL_HIDDEN_LIST:
        return
    with dpg.mutex():
        with dpg.window(
            modal=True,
            autosize=True,
            no_resize=True,
            no_title_bar=True,
        ) as warning:
            dpg.add_text(message, wrap=200)
            dpg.add_separator()
            if checkbox == True:
                dpg.add_checkbox(
                    label="Don't show this again.",
                    callback=lambda: MODAL_HIDDEN_LIST.append(message),
                )
            with dpg.group(horizontal=True):
                dpg.add_button(
                    label="Okay", width=75, callback=lambda: dpg.delete_item(warning)
                )
    dpg.split_frame()
    modal_dimensions = dpg.get_item_rect_size(warning)
    window_dimensions = dpg.get_item_rect_size("Primary Window")
    newPos = [(window_dimensions[i] - modal_dimensions[i]) / 2 for i in range(2)]
    dpg.configure_item(warning, pos=newPos)


class Logger(logging.Handler, metaclass=Singleton):
    """Snazzy"""

    def __init__(self):
        super().__init__()
        self.log_level = 0
        self._auto_scroll = True
        self.count = 0
        self.flush_count = 1000
        self.window_id = dpg.add_window(height=350, width=350, label="Logger")

        with dpg.group(horizontal=True, parent=self.window_id):
            dpg.add_checkbox(
                label="Auto-scroll",
                default_value=True,
                callback=lambda sender: self.auto_scroll(dpg.get_value(sender)),
            )
            dpg.add_button(
                label="Clear",
                callback=lambda: dpg.delete_item(self.filter_id, children_only=True),
            )

        dpg.add_input_text(
            label="Filter (inc, -exc)",
            callback=lambda sender: dpg.set_value(
                self.filter_id, dpg.get_value(sender)
            ),
            parent=self.window_id,
        )
        self.child_id = dpg.add_child_window(
            parent=self.window_id, autosize_x=True, autosize_y=True
        )
        self.filter_id = dpg.add_filter_set(parent=self.child_id)

        with dpg.theme() as self.debug_theme:
            with dpg.theme_component(0):
                dpg.add_theme_color(dpg.mvThemeCol_Text, (64, 128, 255, 255))

        with dpg.theme() as self.info_theme:
            with dpg.theme_component(0):
                dpg.add_theme_color(dpg.mvThemeCol_Text, (255, 255, 255, 255))

        with dpg.theme() as self.warning_theme:
            with dpg.theme_component(0):
                dpg.add_theme_color(dpg.mvThemeCol_Text, (255, 255, 0, 255))

        with dpg.theme() as self.error_theme:
            with dpg.theme_component(0):
                dpg.add_theme_color(dpg.mvThemeCol_Text, (255, 0, 0, 255))

        with dpg.theme() as self.critical_theme:
            with dpg.theme_component(0):
                dpg.add_theme_color(dpg.mvThemeCol_Text, (255, 0, 0, 255))

    def auto_scroll(self, value):
        self._auto_scroll = value

    def _log(self, message, level):
        """Different theme for each level"""
        if level < self.log_level:
            return

        self.count += 1

        if self.count > self.flush_count:
            self.clear_log()

        # TODO: Turn this into a match-case thing
        theme = self.info_theme
        if level == 10:
            theme = self.debug_theme
        elif level == 20:
            pass
        elif level == 30:
            theme = self.warning_theme
        elif level == 40:
            theme = self.error_theme
            modal_message(message)
        elif level == 50:
            theme = self.critical_theme
            modal_message(message)

        new_log = dpg.add_text(
            message, parent=self.filter_id, filter_key=message, wrap=0
        )
        dpg.bind_item_theme(new_log, theme)
        if self._auto_scroll:
            dpg.set_y_scroll(self.child_id, -1.0)

    def emit(self, record):
        string = self.format(record)
        self._log(string, record.levelno)

    def clear_log(self):
        dpg.delete_item(self.filter_id, children_only=True)
        self.count = 0


# I did not write the donut code

theta_spacing = 0.07
phi_spacing = 0.02

R1 = 1
R2 = 2
K2 = 5

screen_width = 35
screen_height = 35

# Calculate K1 based on screen size: the maximum x-distance occurs roughly at
# the edge of the torus, which is at x=R1+R2, z=0.  we want that to be
# displaced 3/8ths of the width of the screen, which is 3/4th of the way from
# the center to the side of the screen.
# screen_width*3/8 = K1*(R1+R2)/(K2+0)
# screen_width*K2*3/(8*(R1+R2)) = K1

K1 = screen_width * K2 * 3 / (8 * (R1 + R2))


def render_frame(A, B):
    # Precompute sines and cosines of A and B
    cosA = math.cos(A)
    sinA = math.sin(A)
    cosB = math.cos(B)
    sinB = math.sin(B)

    char_output = []
    zbuffer = []

    for i in range(screen_height + 1):
        char_output.append([" "] * (screen_width + 0))
        zbuffer.append([0] * (screen_width + 0))

    # theta goes around the cross-sectional circle of a torus
    theta = 0
    while theta < 2 * math.pi:
        theta += theta_spacing

        # Precompute sines and cosines of theta
        costheta = math.cos(theta)
        sintheta = math.sin(theta)

        # phi goes around the center of revolution of a torus
        phi = 0
        while phi < 2 * math.pi:
            phi += phi_spacing

            # Precompute sines and cosines of phi
            cosphi = math.cos(phi)
            sinphi = math.sin(phi)

            # the x,y coordinate of the circle,
            # before revolving (factored out of the above equations)
            circlex = R2 + R1 * costheta
            circley = R1 * sintheta

            # final 3D (x,y,z) coordinate after rotations, directly from our math above
            x = circlex * (cosB * cosphi + sinA * sinB * sinphi) - circley * cosA * sinB
            y = circlex * (sinB * cosphi - sinA * cosB * sinphi) + circley * cosA * cosB
            z = K2 + cosA * circlex * sinphi + circley * sinA
            ooz = 1 / z

            # x and y projection. y is negated here, because y goes up in
            # 3D space but down on 2D displays.
            xp = int(screen_width / 2 + K1 * ooz * x)
            yp = int(screen_height / 2 - K1 * ooz * y)

            # Calculate luminance
            L = (
                cosphi * costheta * sinB
                - cosA * costheta * sinphi
                - sinA * sintheta
                + cosB * (cosA * sintheta - costheta * sinA * sinphi)
            )

            # L ranges from -sqrt(2) to +sqrt(2).  If it's < 0, the surface is
            # pointing away from us, so we won't bother trying to plot it.
            if L > 0:
                # Test against the z-buffer. Larger 1/z means the pixel is closer to
                # the viewer than what's already plotted.
                if ooz > zbuffer[xp][yp]:
                    zbuffer[xp][yp] = ooz
                    luminance_index = (
                        L * 8
                    )  # this brings L into the range 0..11 (8*sqrt(2) = 11.3)

                    # Now we lookup the character corresponding
                    # to the luminance and plot it in our output
                    char_output[xp][yp] = ".,-~:;=!*#$@"[int(luminance_index)]

    # Now, dump char_output to the screen.
    # Bring cursor to "home" location, in just about any currently-used terminal emulation mode
    frame = ""
    for i in range(screen_height):
        for j in range(screen_width):
            frame += char_output[i][j]
        frame += "\n"
    logger.debug(frame)


def logger_stress_test():
    L = Logger()

    def task():
        A = 1.0
        B = 1.0
        for _ in range(250):
            render_frame(A, B)
            A += 0.08
            B += 0.03

    t = threading.Thread(target=task)
    t.start()
