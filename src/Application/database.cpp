#include "application.h"
#include "sqlite3.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

// Helper function to read the content of a file into a string
std::string get_file_contents(const std::string &filename) {
  std::ifstream in(filename, std::ios::in | std::ios::binary);
  if (in) {
    std::ostringstream contents;
    contents << in.rdbuf();
    in.close();
    return contents.str();
  }
  throw std::runtime_error("Failed to open SQL file: " + filename);
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
    std::string sql_script = get_file_contents(sql_filename);
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
