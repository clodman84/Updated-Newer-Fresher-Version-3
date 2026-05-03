#pragma once

#include <SDL3/SDL.h>
#include <filesystem>
#include <sqlite3.h>
#include <string>
#include <vector>

void prepare_database();

enum UNFV3SearchTokenType {
  FTS_SEARCH,
  BHAWAN_SEARCH,
  ID_SEARCH,
  OR,
  AND,
  LPAR,
  RPAR
};

struct ExportInfo {
  std::string bhawan;
  std::string roomno;
};

class Database {
public:
  Database();
  ~Database();

  void read_csv(const std::string &filename);
  void render_loaded_csv();
  void insert_data();
  ExportInfo get_export_information_from_id(std::string id);
  std::vector<std::array<std::string, 4>> evaluate(std::string search_query);
  bool show_loaded_csv = false;
  bool has_credentials();
  bool save_credentials(const std::string &json_content);
  std::string get_credentials();

private:
  using CsvRow = std::array<std::string, 5>;

  std::string modify_query_for_id(std::string query);
  bool parse_csv_row(const std::string &line, CsvRow &row) const;
  bool begin_transaction() const;
  bool commit_transaction() const;
  bool rollback_transaction() const;
  bool execute_sql(const char *sql) const;
  void search(UNFV3SearchTokenType search_type, std::string search_query,
              std::vector<std::array<std::string, 4>> &search_results);
  void clear_loaded_csv();

  const std::filesystem::path db_filename =
      std::filesystem::path(SDL_GetPrefPath("DoPySOFT", "UNFV3")) /
      "database.db";

  std::vector<CsvRow> loaded;
  sqlite3 *db = nullptr;
  sqlite3_stmt *fts_search = nullptr;
  sqlite3_stmt *bhawan_search = nullptr;
  sqlite3_stmt *id_search = nullptr;
  sqlite3_stmt *get_export_info_stmt = nullptr;
  sqlite3_stmt *check_cred_stmt = nullptr;
  sqlite3_stmt *save_cred_stmt = nullptr;
  sqlite3_stmt *get_cred_stmt = nullptr;
};
