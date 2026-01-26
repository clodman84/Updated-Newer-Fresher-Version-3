import Application.database as db

from .billing import generate_bill
from .detect import detect, visualise
from .images import Image, ImageManager
from .music import DJ
from .rename_rolls import rename_rolls
from .search import SearchMachine
from .store import copy_images, load, write
from .utils import ShittyMultiThreading, SimpleTimer
from .image_procesing import (
    colour_balance,
    merge,
    levels,
    get_rgb_histogram,
    split_rgb,
    split_smh,
    get_histogram,
    add,
)
