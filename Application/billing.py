import logging
import pathlib
from collections import Counter

from PIL import Image

from .database import resolve_id

logger = logging.getLogger("Core.CRCBill")


def generate_bill(path: pathlib.Path, bill: Counter):
    logger.debug(path)
    for i in path.iterdir():
        if i.is_dir():
            generate_bill(i, bill)
        elif i.is_file():
            try:
                Image.open(i).verify()
                logger.debug(f"Working on {i}")
                last_four_digits = i.stem[-4:]
                bhawan, roomno = i.stem.split("_")[:2]
                logger.debug(f"{bhawan} {roomno} {last_four_digits}")
                id = resolve_id(last_four_digits, bhawan, roomno)[0]
                bill[id] += 1
                logger.debug(id)
            except Exception as e:
                logger.warning(e, exc_info=True)
                logger.warning("Deleting this shit")
                i.unlink()
    return bill
