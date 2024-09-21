import csv
import logging
import queue
import re
import sqlite3
from datetime import datetime
from pathlib import Path

sqlite3.register_adapter(datetime, lambda x: int(x.timestamp()))
sqlite3.register_converter("timestamp", lambda x: datetime.fromtimestamp(int(x)))

logger = logging.getLogger("Core.Database")


class GarbageMessListError(Exception):
    pass


def connect() -> sqlite3.Connection:
    connection = sqlite3.connect(
        "./Data/data.db", detect_types=sqlite3.PARSE_DECLTYPES, check_same_thread=False
    )
    connection.execute("pragma foreign_keys = ON")
    return connection


class ConnectionPool:
    """
    Allows database connections to be reused

    Attributes:
        _q: A queue.SimpleQueue that stores the connections
    """

    # it will be easier to make this asynchronous in the future, I guess

    _q = queue.SimpleQueue()

    def __enter__(self) -> sqlite3.Connection:
        """
        Gets a connections from ConnectionPool._q, creates a new connection if the queue is empty.

        Returns:

        sqlite3.Connection
        """
        try:
            self.connection = self._q.get_nowait()
        except queue.Empty:
            self.connection = connect()
        return self.connection

    def __exit__(self, exc_type, exc_val, exc_tb):
        """
        Commits the changes if there was no error, rolls back if there was an error. Puts the connection
        back into the queue.

        Args:
            exc_type ():
            exc_val ():
            exc_tb ():
        """
        if exc_type:
            self.connection.rollback()
        else:
            self.connection.commit()
        self._q.put(self.connection)

    @classmethod
    def close(cls):
        """Closes all open database connections"""
        while not cls._q.empty():
            cls._q.get_nowait().close()


def scan_mess_list(path: Path) -> list[dict[str, str]]:
    """
    Verifies that the csv file is a messand returns which datatype of each column
    """

    #TODO: There needs to be instruction in the docs to turn all NULL hoscodes to ps and all NULL roomnos to 0 (a roomno of 1 does not make sense)
    with open(path) as file:
        reader = csv.reader(file)
        rows = [row for row in reader]

    pattern = re.compile(
        r"(?x)(?P<id>\d{4}[a-zA-Z\d]{4}\d{4})|(?P<room>[0-9]{2,4}|0)|(?P<gender>[mMfF]{1})|(?P<name_or_bhawan>^[a-zA-Z_.\-\s]*$)"
    )
    rows = rows[1:]  # we are assuming the first row is all headers
    vals = []
    hoscodes = {
        "ak",
        "bd",
        "bg",
        "cvr",
        "ds",
        "ps",
        "gn",
        "kr",
        "ml",
        "msa",
        "mr",
        "rm",
        "rp",
        "sk",
        "sr",
        "vk",
        "vy",
        "rha",
        "rhb",
        "rhc",
        "rhc",
        "rhd",
        "rhe",
        "rhf",
    }

    for i, row in enumerate(rows):
        val = {}
        for item in row:
            m = pattern.fullmatch(item)
            if not m:
                continue
            else:
                match m.lastgroup:
                    case "id":
                        val["idno"] = item
                    case "room":
                        val["roomno"] = item
                    case "gender":
                        val["gender"] = item
                    case "name_or_bhawan":
                        if item.lower() in hoscodes:
                            val["hoscode"] = item
                        else:
                            val["name"] = item
                val["nick"] = None
        if len(val.keys()) != 6:
            logger.error(f"Something is wrong in row {i+2}\n\nScan results: {val}")
            raise GarbageMessListError
        else:
            vals.append(val)
            logger.debug(val)

    logger.info(f"Scanned {len(vals)} rows and found no discrepencies")
    return vals


def read_mess_list(path: Path):
    """
    Populates the database with values from a csv file.

    Args:
        path: Path to the csv file
    """
    with ConnectionPool() as db:
        vals = scan_mess_list(path)
        db.executemany(
            "INSERT OR IGNORE INTO students (idno, name, gender, hoscode, roomno, nick) VALUES(:idno, :name, :gender, :hoscode, :roomno, :nick)",
            vals,
        )
        db.commit()


def get_file_name(id):
    """
    Returns the file name skeleton which can be formated with the roll number. This
    if the name of the output of the billing process.

    Args:
        id (): The complete ID of the person who's image is being saved.

    Returns:
        str of the form "hoscode_roomno_{}_YEARLAST4DIGITS"

    """
    with ConnectionPool() as db:
        cursor = db.execute(
            "SELECT hoscode, roomno FROM students WHERE idno = ?", (id,)
        )
        hoscode, roomno = cursor.fetchone()
        return f"{hoscode}_{roomno}_{'{}'}{'{:02}'}_{'{}'}_{id[2:4]}{id[-4:]}"


def set_nick(nick, id):
    with ConnectionPool() as db:
        db.execute("UPDATE students SET nick = ? WHERE idno = ?", (nick, id))
        return True


def get_nick(id):
    with ConnectionPool() as db:
        cursor = db.execute("SELECT nick FROM students WHERE idno = ?", (id,))
        return cursor.fetchone()


def get_all_nicks():
    with ConnectionPool() as db:
        cursor = db.execute(
            "SELECT name, idno, nick FROM students WHERE nick IS NOT NULL"
        )
        return cursor.fetchall()
