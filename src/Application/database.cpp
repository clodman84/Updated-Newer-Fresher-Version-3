#include "application.h"
#include "imgui.h"
#include "sqlite3.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

namespace {

std::stringstream get_file_contents(const std::string &filename) {
#ifdef TRACY_ENABLE
  ZoneScopedN("ReadSchemaFile");
#endif
  std::ifstream in(filename, std::ios::in | std::ios::binary);
  if (!in) {
    throw std::runtime_error("Failed to open file: " + filename);
  }

  std::stringstream contents;
  contents << in.rdbuf();
  return contents;
}

void log_sqlite_error(const char *context, sqlite3 *db) {
  std::cerr << context << ": " << sqlite3_errmsg(db) << std::endl;
}

void finalize_statement(sqlite3_stmt *&stmt) {
  if (stmt != nullptr) {
    sqlite3_finalize(stmt);
    stmt = nullptr;
  }
}

} // namespace

Database::Database() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Database::Database");
#endif
  const int rc = sqlite3_open(db_filename.c_str(), &db);
  if (rc != SQLITE_OK) {
    const std::string err =
        db != nullptr ? sqlite3_errmsg(db) : "unknown error";
    sqlite3_close(db);
    db = nullptr;
    throw std::runtime_error("Cannot open database: " + err);
  }

  if (!execute_sql("PRAGMA foreign_keys = ON;")) {
    throw std::runtime_error("Failed to enable foreign keys.");
  }

  const char *fts_sql =
      "SELECT s.idno, s.name, s.hoscode, s.roomno FROM students_fts "
      "f JOIN students s ON s.rowid = f.rowid WHERE students_fts match "
      ":query ORDER BY s.idno";
  const char *bhawan_sql = "SELECT idno, name, hoscode, roomno FROM students "
                           "WHERE hoscode = :query ORDER BY idno";
  const char *id_sql = "SELECT idno, name, hoscode, roomno FROM students WHERE "
                       "idno LIKE :query ORDER BY idno";
  const char *get_info_sql =
      "SELECT hoscode, roomno FROM students WHERE idno = :query";

  if (sqlite3_prepare_v2(db, fts_sql, -1, &fts_search, nullptr) != SQLITE_OK ||
      sqlite3_prepare_v2(db, bhawan_sql, -1, &bhawan_search, nullptr) !=
          SQLITE_OK ||
      sqlite3_prepare_v2(db, id_sql, -1, &id_search, nullptr) != SQLITE_OK ||
      sqlite3_prepare_v2(db, get_info_sql, -1, &get_export_info_stmt,
                         nullptr) != SQLITE_OK) {
    const std::string error = sqlite3_errmsg(db);
    finalize_statement(fts_search);
    finalize_statement(bhawan_search);
    finalize_statement(id_search);
    finalize_statement(get_export_info_stmt);
    sqlite3_close(db);
    db = nullptr;
    throw std::runtime_error(error);
  }
}

Database::~Database() {
  if (db == nullptr) {
    return;
  }

#ifdef TRACY_ENABLE
  ZoneScopedN("Database::~Database");
#endif
  finalize_statement(fts_search);
  finalize_statement(bhawan_search);
  finalize_statement(id_search);
  finalize_statement(get_export_info_stmt);
  sqlite3_close(db);
  db = nullptr;
}

bool Database::parse_csv_row(const std::string &line, CsvRow &row) const {
#ifdef TRACY_ENABLE
  ZoneScopedN("Database::parse_csv_row");
#endif
  std::stringstream ss(line);
  std::string cell;
  std::vector<std::string> cells;
  while (std::getline(ss, cell, ',')) {
    cells.push_back(cell);
  }

  if (cells.size() < row.size()) {
    std::cerr << "Skipping malformed CSV row with " << cells.size()
              << " columns: " << line << std::endl;
    return false;
  }

  for (size_t index = 0; index < row.size(); ++index) {
    row[index] = cells[index];
  }
  return true;
}

void Database::read_csv(const std::string &filename) {
#ifdef TRACY_ENABLE
  ZoneScopedN("Database::read_csv");
#endif
  std::ifstream file(filename);
  if (!file) {
    throw std::runtime_error("Failed to open file: " + filename);
  }

  clear_loaded_csv();

  std::string line;
  std::getline(file, line);

  while (std::getline(file, line)) {
    CsvRow row;
    if (parse_csv_row(line, row)) {
      loaded.push_back(std::move(row));
    }
  }
}

bool Database::execute_sql(const char *sql) const {
  char *err_msg = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err_msg);
  if (rc == SQLITE_OK) {
    return true;
  }

  std::cerr << "SQL error";
  if (err_msg != nullptr) {
    std::cerr << ": " << err_msg;
    sqlite3_free(err_msg);
  } else {
    std::cerr << ": " << sqlite3_errmsg(db);
  }
  std::cerr << std::endl;
  return false;
}

bool Database::begin_transaction() const {
  return execute_sql("BEGIN TRANSACTION;");
}
bool Database::commit_transaction() const { return execute_sql("COMMIT;"); }
bool Database::rollback_transaction() const { return execute_sql("ROLLBACK;"); }

void Database::insert_data() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Database::insert_data");
#endif
  if (loaded.empty()) {
    std::cerr << "No CSV rows loaded; nothing to insert." << std::endl;
    return;
  }

  const char *sql = "INSERT INTO students "
                    "(idno, name, gender, hoscode, roomno, nick) "
                    "VALUES (:idno, :name, :gender, :hoscode, :roomno, NULL) "
                    "ON CONFLICT(idno) "
                    "DO UPDATE SET "
                    "hoscode = excluded.hoscode, "
                    "roomno  = excluded.roomno;";

  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db));
  }

  if (!begin_transaction()) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("Failed to start transaction for CSV import.");
  }

  bool success = true;
  for (const auto &row : loaded) {
    sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":idno"),
                      row[0].c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":name"),
                      row[1].c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":gender"),
                      row[2].c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":hoscode"),
                      row[3].c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":roomno"),
                      row[4].c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
      log_sqlite_error("Failed to insert CSV row", db);
      success = false;
      break;
    }

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
  }

  sqlite3_finalize(stmt);

  if (!success) {
    const bool rolled_back = rollback_transaction();
    if (!rolled_back) {
      std::cerr << "Rollback after failed CSV import also failed." << std::endl;
    }
    throw std::runtime_error("CSV import failed; transaction rolled back.");
  }

  if (!commit_transaction()) {
    const bool rolled_back = rollback_transaction();
    if (!rolled_back) {
      std::cerr << "Rollback after commit failure also failed." << std::endl;
    }
    throw std::runtime_error("Failed to commit CSV import transaction.");
  }

  clear_loaded_csv();
  show_loaded_csv = false;
}

std::string Database::modify_query_for_id(std::string s) {
  const auto is_digit = [](const std::string &str) {
    return std::all_of(str.begin(), str.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; });
  };

  switch (s.length()) {
  case 2:
    s = is_digit(s) ? "20" + s + "%" : "%" + s + "%";
    break;
  case 3:
    s = "%" + s + "%";
    break;
  case 4:
    s = is_digit(s) ? "%" + s : "%" + s + "%";
    break;
  case 6:
    s = is_digit(s) ? "%" + s.substr(0, 2) + "%" + s.substr(2) : "20" + s + "%";
    break;
  default:
    s = "%";
    break;
  }
  return s;
}

void prepare_database() {
#ifdef TRACY_ENABLE
  ZoneScopedN("prepare_database");
#endif
  sqlite3 *database = nullptr;
  std::filesystem::path preferred_path = SDL_GetPrefPath("DoPySOFT", "UNFV3");
  std::filesystem::path db_filename = preferred_path / "database.db";

  std::cout << "Database Path: " << db_filename << '\n';

  // TODO: Migrate to a cross platform resource based thing
  const std::filesystem::path sql_filename = "./Data/schema.sql";

  if (sqlite3_open(db_filename.c_str(), &database) != SQLITE_OK) {
    std::cerr << "Error opening/creating DB: " << sqlite3_errmsg(database)
              << std::endl;
    sqlite3_close(database);
    return;
  }

  try {
    const std::string sql_script = get_file_contents(sql_filename).str();
    char *err_msg = nullptr;
    const int exit_code =
        sqlite3_exec(database, sql_script.c_str(), nullptr, nullptr, &err_msg);
    if (exit_code != SQLITE_OK) {
      std::cerr << "SQL error: "
                << (err_msg != nullptr ? err_msg : sqlite3_errmsg(database))
                << std::endl;
      sqlite3_free(err_msg);
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << std::endl;
  }

  sqlite3_close(database);
}

ExportInfo Database::get_export_information_from_id(std::string idno) {
#ifdef TRACY_ENABLE
  ZoneScopedN("Database::get_export_information_from_id");
#endif
  sqlite3_bind_text(
      get_export_info_stmt,
      sqlite3_bind_parameter_index(get_export_info_stmt, ":query"),
      idno.c_str(), -1, SQLITE_TRANSIENT);

  const int rc = sqlite3_step(get_export_info_stmt);
  ExportInfo info{"unknown", "unknown"};
  if (rc == SQLITE_ROW) {
    const auto *bhawan_text = sqlite3_column_text(get_export_info_stmt, 0);
    const auto *room_text = sqlite3_column_text(get_export_info_stmt, 1);
    info.bhawan = bhawan_text != nullptr
                      ? reinterpret_cast<const char *>(bhawan_text)
                      : "unknown";
    info.roomno = room_text != nullptr
                      ? reinterpret_cast<const char *>(room_text)
                      : "unknown";
  } else {
    std::cerr << "Missing export info for student id: " << idno << std::endl;
  }

  sqlite3_reset(get_export_info_stmt);
  sqlite3_clear_bindings(get_export_info_stmt);
  return info;
}

void Database::clear_loaded_csv() { loaded.clear(); }
