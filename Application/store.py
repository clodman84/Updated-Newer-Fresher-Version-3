import json
import logging
import os
from collections import Counter
from pathlib import Path
from PIL import Image, ImageFont, ImageDraw

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


from PIL import Image, ImageDraw, ImageFont
from pathlib import Path


def write_text_bottom_right(
    image_path: Path,
    target_path: Path,
    text: str,
    font_path: Path = Path("./Fonts/Quantico-Regular.ttf"),
    font_size: int = 100,
    padding: int = 40,
    fill=(255, 255, 255),
    background_fill=(0, 0, 0),
):
    with Image.open(image_path) as img:
        draw = ImageDraw.Draw(img)
        font = ImageFont.truetype(str(font_path), font_size)
        bbox = draw.textbbox((0, 0), text, font=font)

        text_w = bbox[2] - bbox[0]
        text_h = bbox[3] - bbox[1]

        x = img.width - text_w
        y = img.height - text_h - padding

        # Adjust rectangle to bbox origin
        rect = (
            x + bbox[0],
            y + bbox[1],
            x + bbox[2],
            y + bbox[3],
        )
        draw.rectangle(rect, fill=background_fill)
        # Draw text at baseline-correct position
        draw.text((x, y), text, font=font, fill=fill)
        img.save(target_path)


def copy_images(counters: list[Counter], path: Path, source: list[Path]):
    if not path.exists():
        os.mkdir(path)
    for index, image in enumerate(counters):
        for id, count in image.items():
            file_name_base, hoscode, roomno = get_file_name(id)
            for i in range(count):
                file_name = (
                    file_name_base.format(path.name, index + 1, i + 1)
                    + source[index].suffix
                )
                write_text_bottom_right(
                    source[index], path / file_name, f"{hoscode} {roomno}"
                )
                logger.debug(f"Wrote file {file_name}")
