.. _Search Syntax:

Search Syntax
=============

If you directly type out text, the billing window returns a bunch of names, regardless of hostel, year, etc. In order to search for a particular
**ID Number**, you need to enclose it in **forward slashes (/)** (for example, /0758/). For a particular **hostel**, you need to enlcose it in **square brackets ([])** (for example, [GN]).

.. list-table:: Hostel IDs
   :widths: 20 5
   :header-rows: 1

   * - Bhawan
     - Code
   * - Ashok
     - AK
   * - Budh
     - BD
   * - CV Raman
     - CVR
   * - Gandhi
     - GN
   * - Krishna
     - KR
   * - Malviya
     - ML
   * - Malviya Studio Apartments
     - MSA
   * - Meera
     - MR
   * - Ram
     - RM
   * - Rana Pratap
     - RP
   * - Shankar
     - SK
   * - Shrinivas Ramananujan (see the next entry)
     - SR
   * - Vishwakarma (no one calls it that)
     - VK
   * - Vyas
     - VY


Things get more interesting, though. something like /230758/ is a valid query. So is /0758/ and /A80720/ and /B4A3/, etc.
ID is basically represented in the form **([0-9]{2}|)([a-zA-Z][0-9]|)([a-zA-Z][0-9]|)([0-9]{4}|)** (for example, **22B4A30000**).
Therefore, /20230758/ doesnt work. Neither does a full ID like /2023AAPS0758P/.
Higher degree students (masters and phd) can be found by searching for **H[0-9] (masters) and PHXP (phd)**.

Also, when searching for names, no result will be displayed until three letters are typed out. When three letters are typed out, you get every single name
that contains those three letters. So, typing *ush* will give you *usha*, *kaushik* as well as *kushi*. After that every letter further typed gives a name with those letters in succession. (This is because we use SQLite's fts5 engine with the `trigram <https://www.sqlite.org/fts5.html>`_ tokeniser).

You can also use **& (AND/INTERSECTION)** and **| (OR/UNION)** with your queries. If you type surya | [GN], you get all the people who's name contains surya,
*OR* they live in gandhi. If you type surya & [GN], you get all the people who's name contains surya *AND* they live in gandhi. The evaluation goes from
left to right, and *unions are the outermost statement*, meaning *it will be evaluated with the least priority*. Brackets can also be added, which adds priority
(surya | [GN]) & /0758/ will evaluate the brackets first, and then move on.

As an example,
**ram & /b4/ & [GN]|[KR]** queries all the people with ram in their name *AND* have the b4 branch *AND* live in gandhi, *OR* the people living in krishna.
Basically it becomes **(ram & /b4/ & [GN])|([KR])**.

.. image :: Parser.webp
   :width: 800
