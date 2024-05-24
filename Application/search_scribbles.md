this is a where I thought about the searchbar grammar while writing search.py
there is barely any documentation inside so just read this I guess

# This is a list of valid expressions for the search functions

## SET (these evaluate to the lowest level sql queries)

shourin chakraborty -> Just a name that searches via fts
23* -> matches the IDs
name matching: basic fts search
id matching: this is little tricky as there needs to be an idno MATCH str that needs to change depending on the type of filter
|- year
|- branch
|- fd, hd, phd, mba

bhawan matching: basic is search

GN, VK, etc
23*, 23B4*, 23B4A3, etc
Shourin Chakraborty

name -> not enclosed in anything
id -> enclosed in //: /B4A3/
bhawan -> enclosed in []: [GN]

/231053/ should also be a valid search query

id has a lot of variation within it

-> implementation
%23%
%23B4%
%23%1053
%B4%

there are always 2 '%'s in the LIKE string

the year and branch go to the right of 1% and left of 2% (branch after year)
id is the only thing that goes to the right of 2%

All of these evaluate to python sets

## & (INTERSECTION)

/23/ & GN -> Valid
[GN] & /23/ -> Valid
Shourin & /b4a7/ -> Valid
/b4a7/ & Shourin -> Valid

shourin & /b4/ & [GN] -> Valid

evaluation order - shourin & (/b4/ & [GN])
unions are commutative anyways

## | (UNION)

UNION of two low level queries
shourin & /b4/ & ([GN]|[KR])

## Brackets

Expressions

([GN] | [KR]) & /b4/

# Some Example Strings

(shourin & GN) & b4

ram & /b4/ & [GN]|[KR]


# Crystallising all of this into a PEG in infix notation

```
query_set :: fts_name or id or bhawan
set :: query_set or '(' union ')'
intersection :: set ('&' set)*
union :: intersection ('|' intersection)*
```
where fts_name is a Word(alpha)

id is a /Word(alphanum)/ that matches "([0-9]{2,4}|)([a-zA-Z][0-9]|)([a-zA-Z][0-9]|)([0-9]{4}|)" -> this has 4 groups corresponding to each part of the id

bhawan is [Word(alpha)]
