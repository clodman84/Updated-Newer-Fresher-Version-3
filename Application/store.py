import json
import logging
from collections import Counter
from pathlib import Path

logger = logging.getLogger("Core.Store")


def write(counters: list[Counter], roll: str):
    with open(Path(f"./Data/{roll}.json"), "w") as file:
        json.dump(counters, file)


def load(roll: str):
    path = Path(f"./Data/{roll}.json")
    if path.exists():
        with open(path) as file:
            return [Counter(i) for i in json.load(file)]
