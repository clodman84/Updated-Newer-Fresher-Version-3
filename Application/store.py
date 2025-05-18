import json
import logging
import os
from collections import Counter
from pathlib import Path
from shutil import copy

from .database import get_file_name

logger = logging.getLogger("Core.Store")


def write(counters: list[Counter], roll: str):
    with open(Path(f"./Data/{roll}.json"), "w") as file:
        json.dump(counters, file)


def load(roll: str):
    path = Path(f"./Data/{roll}.json")
    if path.exists():
        with open(path) as file:
            return [Counter(i) for i in json.load(file)]


def copy_images(counters: list[Counter], path: Path, source: list[Path]):
    if not path.exists():
        os.mkdir(path)
    for index, image in enumerate(counters):
        for id, count in image.items():
            file_name_base = get_file_name(id)
            for i in range(count):
                file_name = (
                    file_name_base.format(path.name, index + 1, i + 1)
                    + source[index].suffix
                )
                logger.debug(file_name)
                copy(source[index], path / file_name)
