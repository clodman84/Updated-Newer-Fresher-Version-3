#include "application.h"
#include <algorithm>
#include <string>
#include <vector>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

typedef struct {
  TokenType type;
  std::string value;
} Token_T;

// Tokens
// ======
//
// /2023 -> id
// [gn  -> bhawan
// shour -> name
//
// Operators
// =========
//
// &
// |
//
// Extra
// =====
// ()
// " "
//

enum State { WAITING, READING_ID, READING_BHAWAN, READING_NAME };

void Database::search(TokenType search_type, std::string search_query,
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
  default:
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
      break;
    }
  }
  sqlite3_reset(stmt);
}

std::vector<Token_T> lex(std::string search_query) {

#ifdef TRACY_ENABLE
  ZoneScopedN("Lex Query");
#endif

  std::vector<Token_T> output;
  TokenType type;
  std::string value;
  State current_state = WAITING;
  int idx = 0;
  int len = search_query.length();

  for (const auto character : search_query) {
    idx++;
    if (current_state == WAITING) {
      if (character == '/') {
        current_state = READING_ID;
        type = ID_SEARCH;
        continue;
      } else if (character == '[') {
        current_state = READING_BHAWAN;
        type = BHAWAN_SEARCH;
        continue;
      } else if (character == ' ') {
        current_state = WAITING;
        continue;
      } else if (character == '&') {
        value = character;
        output.push_back({AND, value});
        value = "";
        continue;
      } else if (character == '|') {
        value = character;
        output.push_back({OR, value});
        value = "";
        continue;
      } else if (character == '(') {
        value = character;
        output.push_back({LPAR, value});
        value = "";
        continue;
      } else if (character == ')') {
        value = character;
        output.push_back({RPAR, value});
        value = "";
        continue;
      } else {
        current_state = READING_NAME;
        value = character;
        type = FTS_SEARCH;
        continue;
      }
    }
    if ((current_state == READING_ID) | (current_state == READING_BHAWAN) |
        (current_state == READING_NAME)) {
      if (character == ' ') {
        current_state = WAITING;
        output.push_back({type, value});
        value = "";
      } else if (character == ')') {
        current_state = WAITING;
        output.push_back({type, value});
        output.push_back({RPAR, std::string{')'}});
        value = "";
      } else if (idx == len) {
        value += character;
        current_state = WAITING;
        output.push_back({type, value});
        value = "";
      } else {
        value += character;
      }
    }
  }

  return output;
}

// return token stream in reverse polish notation
std::vector<Token_T> parse(std::vector<Token_T> tokens) {

#ifdef TRACY_ENABLE
  ZoneScopedN("ParseQuery");
#endif

  std::vector<Token_T> output;
  std::vector<Token_T> operator_stack;
  for (const auto token : tokens) {
    switch (token.type) {
    case FTS_SEARCH:
    case BHAWAN_SEARCH:
    case ID_SEARCH:
      output.push_back(token);
      break;
    case OR:
      while (!operator_stack.empty() && (operator_stack.back().type != LPAR) &&
             (operator_stack.back().type == AND)) {
        output.push_back(operator_stack.back());
        operator_stack.pop_back();
      }
      operator_stack.push_back(token);
      break;
    case AND:
      operator_stack.push_back(token);
      break;
    case LPAR:
      operator_stack.push_back(token);
      break;
    case RPAR:
      while (!operator_stack.empty() && (operator_stack.back().type != LPAR)) {
        output.push_back(operator_stack.back());
        operator_stack.pop_back();
      }
      if (!operator_stack.empty() && (operator_stack.back().type == LPAR))
        operator_stack.pop_back();
      break;
    }
  }
  output.insert(output.end(), operator_stack.begin(), operator_stack.end());
  return output;
}

void Session::evaluate() {

#ifdef TRACY_ENABLE
  ZoneScopedN("Evaluate Query");
#endif

  std::vector<Token_T> tokens = parse(lex(search_query));
  std::vector<std::vector<std::array<std::string, 4>>> result_stack;

  for (const auto token : tokens) {
    switch (token.type) {
    case FTS_SEARCH:
    case BHAWAN_SEARCH:
    case ID_SEARCH:
      database->search(token.type, token.value, search_results);
      result_stack.push_back(search_results);
      break;
    case AND:
      if (result_stack.size() >= 2) {

        auto left = result_stack.back();
        result_stack.pop_back();
        auto right = result_stack.back();
        result_stack.pop_back();
        std::vector<std::array<std::string, 4>> results;
        std::set_intersection(left.begin(), left.end(), right.begin(),
                              right.end(), std::back_inserter(results));
        result_stack.push_back(results);
      }
      break;
    case OR:
      if (result_stack.size() >= 2) {
        auto left = result_stack.back();
        result_stack.pop_back();
        auto right = result_stack.back();
        result_stack.pop_back();
        std::vector<std::array<std::string, 4>> results;
        std::set_union(left.begin(), left.end(), right.begin(), right.end(),
                       std::back_inserter(results));
        result_stack.push_back(results);
      }
      break;
    default:
      break;
    }
  }
  if (!result_stack.empty())
    search_results = result_stack.back();
}
