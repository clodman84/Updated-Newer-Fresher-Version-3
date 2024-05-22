# this might be merged into databases.py later
import logging

from .database import ConnectionPool

logger = logging.getLogger("Application.Search")


def search(text: str):
    # this is meant to use the full text search capabilities of sqlite to
    # autocomplete in the file dialog
    with ConnectionPool() as db:
        fts_text = "SELECT name, rowid from students_fts WHERE name MATCH ? ORDER BY rank LIMIT 15"
        id_text = "SELECT name, rowid from students WHERE idno LIKE ? LIMIT 15"
        if text.replace(" ", "").isalpha():
            res = db.execute(fts_text, (text,))
        else:
            res = db.execute(id_text, (text + "%",))
        return res.fetchall()
