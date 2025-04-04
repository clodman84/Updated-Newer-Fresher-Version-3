import logging
from pathlib import Path

import dearpygui.dearpygui as dpg

from Application import SearchMachine, copy_images, db, load, write
from Application.utils import SimpleTimer
from GUI.tablez import TableManager9000
from GUI.utils import modal_message

logger = logging.getLogger("GUI.Fuckups")
