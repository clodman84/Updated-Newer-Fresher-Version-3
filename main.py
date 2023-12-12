import logging

import dearpygui.dearpygui as dpg

import GUI


def main():
    dpg.create_context()
    dpg.create_viewport(title="People Simulator")
    SimulationLogger = logging.getLogger("Simulation")
    GUI_Logger = logging.getLogger("GUI")
    SimulationLogger.setLevel(logging.DEBUG)
    GUI_Logger.setLevel(logging.DEBUG)
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
                SimulationLogger.addHandler(log)
                GUI_Logger.addHandler(log)

    dpg.setup_dearpygui()
    dpg.set_primary_window("Primary Window", True)
    dpg.set_viewport_vsync(False)
    dpg.show_viewport(maximized=True)
    dpg.start_dearpygui()
    dpg.destroy_context()


if __name__ == "__main__":
    main()
