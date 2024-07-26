import logging
import re
from collections import namedtuple
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

SearchResult = namedtuple("SearchResult", ["name", "idno", "hoscode", "roomno"])


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
            return {SearchResult(*item) for item in cursor}

    def get_id(self, argument):
        param = "%"
        if len(argument) == 3:
            param = "%" + argument + "%"
        elif len(argument) == 6:
            if argument.isdigit():
                param = "%" + argument[:2] + "%" + argument[2:6]
            else:
                param = (
                    "20" + argument + "%"
                )  # if bits and dopy exists a 1000 years later, change the 20 to 30
        elif len(argument) == 2:
            if argument.isdigit():
                param = "20" + argument + "%"
            else:
                param = "%" + argument + "%"
        elif len(argument) == 4:
            if argument.isdigit():
                param = "%" + argument
            else:
                param = "%" + argument + "%"
        else:
            return None
        # logger.debug(param)
        with ConnectionPool() as db:
            cursor = db.execute(self.id_text, (param,))
            return {SearchResult(*item) for item in cursor}

    def get_bhawan(self, argument):
        with ConnectionPool() as db:
            cursor = db.execute(self.bhawan_text, (argument,))
            return {SearchResult(*item) for item in cursor}

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

    def search(self, text) -> set[SearchResult] | None:
        try:
            return self.evaluate(self.parser(text))
        except ParseException:
            return None
