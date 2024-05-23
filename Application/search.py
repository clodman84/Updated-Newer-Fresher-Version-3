import logging

from .database import ConnectionPool

logger = logging.getLogger("Application.Search")

# TODO: Implement a simple language parser that can be used to search and filter people


def search(text: str):
    # this is meant to use the parser and the full text search capabilities of sqlite to
    # autocomplete in the file dialog

    with ConnectionPool() as db:
        fts_text = f"SELECT name, idno, hoscode, roomno FROM students WHERE rowid IN (SELECT rowid FROM students_fts WHERE name MATCH ? ORDER BY rank LIMIT 15)"
        id_text = f"SELECT name, idno, hoscode, roomno from students WHERE idno LIKE ? LIMIT 15"
        if text.replace(" ", "").isalpha():
            res = db.execute(fts_text, (text,))
        else:
            res = db.execute(id_text, (text + "%",))
        return res.fetchall()
