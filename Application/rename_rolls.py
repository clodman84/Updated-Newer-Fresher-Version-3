import itertools
import logging
import pathlib

from PIL import Image

from .database import get_hoscode_roomno_from_short_id

logger = logging.getLogger("Core.Genie")


def rename_rolls(path: pathlib.Path, dubious_list: list):
    logger.debug(path)
    for i in path.iterdir():
        if i.is_dir():
            rename_rolls(i, dubious_list)
        elif i.is_file():
            try:
                # this bit just fixes a weird bug
                if i.name[:2] == "._":
                    new_name = i.with_name(i.name[2:])
                    i.rename(new_name)
                    logger.debug(f"{i.name} was renamed to {new_name}")

                if "PS" not in i.name:
                    continue

                Image.open(i).verify()
                logger.debug(f"Working on {i}")
                id = i.stem[-6:]
                bruh = get_hoscode_roomno_from_short_id(id)
                logger.debug(bruh)
                if len(bruh) > 1:
                    dubious_list.append((i, [j[2] for j in bruh]))
                    continue

                hoscode, roomno, _ = bruh[0]
                new_name = f"{hoscode}_{roomno}_{'_'.join(itertools.islice(i.name.split('_'), 2, None))}"
                if new_name != i.name:
                    new_name = i.with_name(new_name)
                    i.rename(new_name)
                    logger.debug(f"{i.name} was renamed to {new_name}")

            except Exception as e:
                logger.warning(e, exc_info=True)

    return dubious_list
