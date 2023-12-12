import queue
import sqlite3
from datetime import datetime

sqlite3.register_adapter(datetime, lambda x: int(x.timestamp()))
sqlite3.register_converter("timestamp", lambda x: datetime.fromtimestamp(int(x)))


def connect() -> sqlite3.Connection:
    connection = sqlite3.connect(
        "data.db", detect_types=sqlite3.PARSE_DECLTYPES, check_same_thread=False
    )
    connection.execute("pragma foreign_keys = ON")
    return connection


class ConnectionPool:
    # it will be easier to make this asynchronous in the future, I guess
    _q = queue.SimpleQueue()

    def __enter__(self) -> sqlite3.Connection:
        try:
            self.connection = self._q.get_nowait()
        except queue.Empty:
            self.connection = connect()
        return self.connection

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type:
            self.connection.rollback()
        else:
            self.connection.commit()
        self._q.put(self.connection)

    @classmethod
    def close(cls):
        while not cls._q.empty():
            cls._q.get_nowait().close()


def setup_db():
    with open("schema.sql") as file:
        query = "".join(file.readlines())
    with connect() as connection:
        connection.executescript(query)
    connection.close()

def read_mess_list():
    with ConnectionPool() as connection:
        


# this is out here on purpose
setup_db()

