#include "application.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "sqlite3.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
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

  // Start transaction for speed
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

void Database::show_loaded_csv() {
  ImGui::Begin("Loaded Mess List");
  ImGui::Text(
      "%lu lines of csv parsed, scroll to the bottom to load this mess list",
      loaded.size());
  ImGui::BeginTable("loaded", 5,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit);
  ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 120.0f);
  ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
  ImGui::TableSetupColumn("Gender", ImGuiTableColumnFlags_WidthFixed, 80.0f);
  ImGui::TableSetupColumn("Bhawan", ImGuiTableColumnFlags_WidthFixed, 80.0f);
  ImGui::TableSetupColumn("Room No", ImGuiTableColumnFlags_WidthFixed, 80.0f);

  ImGui::TableHeadersRow();
  for (std::vector<std::string> line : loaded) {
    ImGui::TableNextRow();
    for (std::string item : line) {
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
