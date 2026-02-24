CREATE TABLE IF NOT EXISTS students(
  IDNO text UNIQUE,
  NAME text,
  GENDER text,
  HOSCODE text,
  ROOMNO text,
  NICK text UNIQUE
);

-- full text search capabilities for students
DROP table IF EXISTS students_fts;
CREATE VIRTUAL TABLE students_fts USING fts5(NAME, NICK, IDNO UNINDEXED, content='students', tokenize='trigram');

DROP TRIGGER IF EXISTS students_ai;
DROP TRIGGER IF EXISTS students_ad;
DROP TRIGGER IF EXISTS students_au;

-- Triggers to keep the FTS index up to date.
CREATE TRIGGER students_ai AFTER INSERT ON students BEGIN
  INSERT INTO students_fts(rowid, NAME, NICK, IDNO) VALUES (new.rowid, new.NAME , new.NICK, new.IDNO);
END;
CREATE TRIGGER students_ad AFTER DELETE ON students BEGIN
  INSERT INTO students_fts(students_fts, rowid, NAME, NICK, IDNO) VALUES('delete', old.rowid, old.NAME, old.NICK, old.IDNO);
END;
CREATE TRIGGER students_au AFTER UPDATE ON students BEGIN
  INSERT INTO students_fts(students_fts, rowid, NAME, NICK, IDNO) VALUES('delete', old.rowid, old.NAME, old.NICK, old.IDNO);
  INSERT INTO students_fts(rowid, NAME, NICK, IDNO) VALUES (new.rowid, new.NAME, new.NICK, new.IDNO);
END;

INSERT INTO students_fts(students_fts) VALUES('rebuild');
