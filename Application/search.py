import logging

from pyparsing import (
    Combine,
    Forward,
    Group,
    Suppress,
    Word,
    ZeroOrMore,
    alphanums,
    alphas,
)

from .database import ConnectionPool

logger = logging.getLogger("Application.Search")


def parser():
    expression = Forward()

    querry_set = (
        Group(Combine(Word(alphas) + ZeroOrMore(" " + Word(alphas)))).setResultsName(
            "name"
        )
        | Group(
            Combine(Suppress("/") + Word(alphanums) + Suppress("/"))
        ).setResultsName("id")
        | Group(Combine(Suppress("[") + Word(alphas) + Suppress("]"))).setResultsName(
            "bhawan"
        )
        | Group(Combine(Suppress("(") + expression + Suppress(")"))).setResultsName(
            "parenthesised"
        )
    )

    operatorOr = Forward()
    operatorOr <<= (
        Group(querry_set + Suppress("|") + operatorOr).setResultsName("or") | querry_set
    )

    operatorAnd = Forward()
    operatorAnd <<= (
        Group(operatorOr + Suppress("&") + operatorAnd).setResultsName("and")
        | operatorOr
    )

    expression <<= operatorAnd

    return expression.parse_string


def search(text: str):
    # this is meant to use the parser and the full text search capabilities of sqlite to
    # autocomplete in the file dialog

    with ConnectionPool() as db:
        fts_text = f"SELECT name, idno, hoscode, roomno FROM students WHERE rowid IN (SELECT rowid FROM students_fts WHERE name MATCH ? ORDER BY rank)"
        id_text = f"SELECT name, idno, hoscode, roomno from students WHERE idno LIKE ?"
        if text.replace(" ", "").isalpha():
            res = db.execute(fts_text, (text,))
        else:
            res = db.execute(id_text, (text + "%",))
        return res.fetchall()


p = parser()
a = p("(Shourin & /23b4a3/) | [gn]")
print(a.as_dict())
