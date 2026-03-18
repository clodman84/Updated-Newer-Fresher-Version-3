#include "application.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "sqlite3.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Helper function to read the content of a file into memory
std::stringstream get_file_contents(const std::string &filename) {
  std::ifstream in(filename, std::ios::in | std::ios::binary);
  if (in) {
    std::stringstream contents;
    contents << in.rdbuf();
    in.close();
    return contents;
  }
  throw std::runtime_error("Failed to open file: " + filename);
}

Database::Database() {
  int rc = sqlite3_open(db_filename.c_str(), &db);
  if (rc != SQLITE_OK) {
    std::string err = sqlite3_errmsg(db);
    sqlite3_close(db);
    throw std::runtime_error("Cannot open database: " + err);
  }
  // Enable foreign keys (recommended)
  sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
  const char *fts_sql =
      "SELECT s.idno, s.name, s.hoscode, s.roomno FROM students_fts "
      "f JOIN students s ON s.rowid = f.rowid WHERE students_fts match "
      ":query ORDER BY s.idno";

  const char *bhawan_sql = "SELECT idno, name, hoscode, roomno FROM students "
                           "WHERE hoscode = :query ORDER BY idno";

  const char *id_sql = "SELECT idno, name, hoscode, roomno FROM students WHERE "
                       "idno LIKE :query ORDER BY idno";

  if (sqlite3_prepare_v2(db, fts_sql, -1, &fts_search, nullptr) != SQLITE_OK)
    throw std::runtime_error(sqlite3_errmsg(db));

  if (sqlite3_prepare_v2(db, bhawan_sql, -1, &bhawan_search, nullptr) !=
      SQLITE_OK)
    throw std::runtime_error(sqlite3_errmsg(db));

  if (sqlite3_prepare_v2(db, id_sql, -1, &id_search, nullptr) != SQLITE_OK)
    throw std::runtime_error(sqlite3_errmsg(db));
}

Database::~Database() {
  if (db) {
    sqlite3_close(db);
    db = nullptr;
  }
}

void Database::read_csv(const std::string &filename) {
  std::ifstream file(filename);
  if (!file)
    throw std::runtime_error("Failed to open file");

  std::string line;

  // skip header
  std::getline(file, line);

  while (std::getline(file, line)) {
    std::vector<std::string> row;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) {
      row.push_back(cell);
    }
    loaded.push_back(std::move(row));
  };
}

void Database::insert_data() {
  const char *sql = "INSERT INTO students "
                    "(idno, name, gender, hoscode, roomno, nick) "
                    "VALUES (:idno, :name, :gender, :hoscode, :roomno, NULL) "
                    "ON CONFLICT(idno) "
                    "DO UPDATE SET "
                    "hoscode = excluded.hoscode, "
                    "roomno  = excluded.roomno;";

  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    throw std::runtime_error(sqlite3_errmsg(db));

  sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
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
    if (sqlite3_step(stmt) != SQLITE_DONE)
      throw std::runtime_error(sqlite3_errmsg(db));
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
  }

  sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
  sqlite3_finalize(stmt);
}

std::string Database::modify_query_for_id(std::string s) {
  auto is_digit = [](const std::string &str) {
    return std::all_of(str.begin(), str.end(),
                       [](unsigned char c) { return std::isdigit(c); });
  };

  switch (s.length()) {
  case 2:
    if (is_digit(s))
      s = "20" + s + "%";
    else
      s = "%" + s + "%";
    break;

  case 3:
    s = "%" + s + "%";
    break;

  case 4:
    if (is_digit(s))
      s = "%" + s;
    else
      s = "%" + s + "%";
    break;

  case 6:
    if (is_digit(s))
      s = "%" + s.substr(0, 2) + "%" + s.substr(2);
    else
      s = "20" + s + "%";
    break;

  default:
    s = "%";
    break;
  }
  return s;
}

void Database::search(TokenType search_type, std::string &search_query,
                      std::vector<std::array<std::string, 4>> &search_results) {
  sqlite3_stmt *stmt;
  std::string query;
  switch (search_type) {
  case FTS_SEARCH:
    query = search_query;
    stmt = fts_search;
    break;
  case BHAWAN_SEARCH:
    query = search_query;
    stmt = bhawan_search;
    break;
  case ID_SEARCH:
    query = modify_query_for_id(search_query);
    stmt = id_search;
    break;
  case OPERATOR:
    return;
  }

  sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":query"),
                    query.c_str(), -1, SQLITE_TRANSIENT);
  search_results.clear();
  while (true) {
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      std::array<std::string, 4> line;
      for (int i = 0; i < 4; i++) {
        const unsigned char *text = sqlite3_column_text(stmt, i);

        line[i] = text ? std::string(reinterpret_cast<const char *>(text)) : "";
      }
      search_results.push_back(line);
    } else if (rc == SQLITE_DONE) {
      break;
    } else {
      std::cerr << "Step error: " << sqlite3_errmsg(db) << "\n";
      break;
    }
  }
  sqlite3_reset(stmt);
}

void Database::render_loaded_csv() {
  if (!show_loaded_csv)
    return;
  if (!ImGui::Begin("Loaded Mess List", &show_loaded_csv)) {
    ImGui::End();
    return;
  }
  ImGui::Text(
      "%lu lines of csv parsed, scroll to the bottom to load this mess list",
      loaded.size());
  ImGui::BeginTable("##", 5,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit);
  ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 120.0f);
  ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
  ImGui::TableSetupColumn("Sex", ImGuiTableColumnFlags_WidthFixed, 40.0f);
  ImGui::TableSetupColumn("Bhawan", ImGuiTableColumnFlags_WidthFixed, 50.0f);
  ImGui::TableSetupColumn("Room", ImGuiTableColumnFlags_WidthFixed, 40.0f);

  ImGui::TableHeadersRow();
  for (const auto &line : loaded) {
    ImGui::TableNextRow();
    for (const auto &item : line) {
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(item.c_str());
    }
  }
  ImGui::EndTable();
  if (ImGui::Button("Looks good to me, load this messlist"))
    this->insert_data();
  ImGui::End();
}

void prepare_database() {
  sqlite3 *DB;
  std::string db_filename = "./Data/database.db";
  std::string sql_filename = "./Data/schema.sql";

  int exit_code = sqlite3_open(db_filename.c_str(), &DB);
  if (exit_code) {
    std::cerr << "Error opening/creating DB: " << sqlite3_errmsg(DB)
              << std::endl;
  } else {
    std::cout << "Opened/Created Database Successfully!" << std::endl;
  }

  try {
    std::string sql_script = get_file_contents(sql_filename).str();
    const char *sql = sql_script.c_str();
    char *err_msg = nullptr;
    exit_code = sqlite3_exec(DB, sql, nullptr, 0, &err_msg);

    if (exit_code != SQLITE_OK) {
      std::cerr << "SQL error: " << err_msg << std::endl;
      sqlite3_free(err_msg);
    } else {
      std::cout << "SQL script executed successfully!" << std::endl;
    }
  } catch (const std::runtime_error &e) {
    std::cerr << e.what() << std::endl;
  }

  sqlite3_close(DB);
}
