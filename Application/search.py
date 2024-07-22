import logging
import re
from functools import reduce

from pyparsing import (
    Combine,
    Forward,
    Group,
    ParseException,
    Suppress,
    Word,
    ZeroOrMore,
    alphanums,
    alphas,
)

from .database import ConnectionPool

logger = logging.getLogger("Core.Search")


def parser():
    """
    query_set :: fts_name or id or bhawan
    set :: query_set or '(' union ')'
    intersection :: set ('&' set)*
    union :: intersection ('|' intersection)*
    """

    union = Forward()
    lpar, rpar = map(Suppress, "()")
    querry_set = (
        Group(Combine(Word(alphas) + ZeroOrMore(" " + Word(alphas)))).set_results_name(
            "name"
        )
        | Group(
            Combine(Suppress("/") + Word(alphanums) + Suppress("/"))
        ).set_results_name("id")
        | Group(Combine(Suppress("[") + Word(alphas) + Suppress("]"))).set_results_name(
            "bhawan"
        )
    )
    Set = querry_set | Group(lpar + union + rpar).set_results_name("set")
    intersection = Group(Set + ZeroOrMore(Suppress("&") + Set)).set_results_name("and")
    union <<= Group(
        intersection + ZeroOrMore(Suppress("|") + intersection)
    ).set_results_name("or")
    return union.parse_string


def set_and(x, y):
    return x & y


def set_or(x, y):
    return x | y


class SearchMachine:
    def __init__(self) -> None:
        self.parser = parser()
        self.fts_text = """SELECT name, idno, hoscode, roomno FROM students WHERE rowid IN (SELECT * from (SELECT rowid FROM students_fts WHERE name MATCH ? ORDER BY rank) UNION SELECT * from (SELECT rowid FROM students_fts WHERE nick MATCH ? ORDER BY rank))"""
        self.id_text = (
            f"SELECT name, idno, hoscode, roomno from students WHERE idno LIKE ?"
        )
        self.bhawan_text = (
            f"SELECT name, idno, hoscode, roomno from students WHERE hoscode LIKE ?"
        )
        self.id_regex = re.compile(
            r"([0-9]{2}|)([a-zA-Z][a-zA-Z0-9]|)([a-zA-Z][a-zA-Z0-9]|)([0-9]{4}|)"
        )

    def get_name(self, argument):
        with ConnectionPool() as db:
            cursor = db.execute(
                self.fts_text, (argument, argument)
            )  # argument, argument since the querry uses the search term twice :(
            return {tuple(item) for item in cursor}

    def get_id(self, argument):
        param = "%"
        argument = argument
        m = self.id_regex.match(argument)
        if not m:
            return None
        j = 0
        for i in range(1, 5):
            c = m.group(i)
            if not c:
                if i == 3 and j != 3:
                    param += "%"
                continue
            param += c
            j += 1
        param += "%"
        with ConnectionPool() as db:
            cursor = db.execute(self.id_text, (param,))
            return {tuple(item) for item in cursor}

    def get_bhawan(self, argument):
        with ConnectionPool() as db:
            cursor = db.execute(self.bhawan_text, (argument,))
            return {tuple(item) for item in cursor}

    def evaluate(self, argument):
        c = argument.get_name()
        match c:
            case "or":
                if len(argument) > 1:
                    return reduce(set_or, (self.evaluate(i) for i in argument))
                else:
                    return self.evaluate(argument[0])
            case "and":
                if len(argument) > 1:
                    return reduce(set_and, (self.evaluate(i) for i in argument))
                else:
                    return self.evaluate(argument[0])
            case "name":
                return self.get_name(argument[0])
            case "bhawan":
                return self.get_bhawan(argument[0])
            case "id":
                return self.get_id(argument[0])
            case "set":
                return self.evaluate(argument[0])

    def search(self, text):
        try:
            return self.evaluate(self.parser(text))
        except ParseException:
            pass
