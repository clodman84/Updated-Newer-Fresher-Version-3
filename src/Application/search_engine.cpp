#include "application.h"
#include <iostream>
#include <string>
#include <vector>

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

std::vector<Token_T> lex(std::string search_query) {
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
      } else if ((character == '&') | (character == '|')) {
        value = character;
        output.push_back({OPERATOR, value});
        value = "";
        continue;
      } else if ((character == '(') | (character == ')')) {
        value = character;
        output.push_back({GRAMMAR, value});
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
      value += character;
      if ((character == ' ') | (idx == len)) {
        output.push_back({type, value});
        current_state = WAITING;
        value = "";
      }
    }
  }

  return output;
}

// return token stream in reverse polish notation
std::vector<Token_T> parse(std::vector<Token_T>);

void Session::evaluate() {
  std::vector<Token_T> tokens = lex(search_query);
  std::cout << "====tokens==============\n";
  for (auto token : tokens) {
    std::cout << "Token Type: " << token.type << " Value " << token.value
              << '\n';
  }
  std::cout << "========================\n";

  if (!tokens.empty()) {
    Token_T last = tokens.back();
    database->search(last.type, last.value, search_results);
  }
}
