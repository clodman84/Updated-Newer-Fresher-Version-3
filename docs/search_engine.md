# UNFV3 Search Engine: Shunting-Yard & RPN

The UNFV3 search engine implements a robust boolean query parser using the **Shunting-Yard algorithm** to convert infix notation (user-friendly) into Postfix/Reverse Polish Notation (RPN), which is then evaluated using a stack-based approach.

## 1. Tokenization (Lexing)

The `lex()` function in `search_engine.cpp` scans the input string and identifies the following tokens:

| Token Type | Prefix | Description | Example |
| :--- | :--- | :--- | :--- |
| **ID_SEARCH** | `/` | Searches for a specific student ID. | `/2023A7PS0001` |
| **BHAWAN_SEARCH** | `[` | Filters by hostel/bhawan name. | `[Vyas` |
| **FTS_SEARCH** | *None* | Full-text search (default for names). | `Rahul` |
| **AND** | `&` | Boolean intersection operator. | `A & B` |
| **OR** | `|` | Boolean union operator. | `A | B` |
| **LPAR / RPAR** | `(` / `)` | Parentheses for grouping operations. | `(A | B) & C` |

## 2. The Shunting-Yard Algorithm

The `parse()` function implements the **Shunting-Yard algorithm**, originally developed by Edsger Dijkstra. It converts the stream of tokens from infix (e.g., `A & (B | C)`) to Postfix/RPN (e.g., `A B C | &`).

### Internal Logic:
- **Operands** (Search terms): Immediately pushed to the output queue.
- **Operators** (`&`, `|`):
  - If the incoming operator has lower or equal precedence than the operator at the top of the stack, the stack operator is popped to the output queue.
  - Precedence: `&` (AND) is higher than `|` (OR).
- **Parentheses**:
  - `(` is pushed onto the operator stack.
  - `)` triggers popping from the operator stack to the output queue until a matching `(` is found.

**Resources:**
- [Wikipedia: Shunting-Yard Algorithm](https://en.wikipedia.org/wiki/Shunting-yard_algorithm)
- [Infix to Postfix Conversion](https://www.geeksforgeeks.org/convert-infix-expression-to-postfix-expression/)

## 3. RPN Evaluation

The `Session::evaluate()` method processes the Postfix token stream using a **Result Stack**.

1.  **Search Tokens**: When an operand (ID, Bhawan, or Name) is encountered, `Database::search()` is called. the resulting `std::vector` of rows is pushed onto the `result_stack`.
2.  **Operators**:
    - **AND (`&`)**: Pops the top two result sets from the stack, computes their **intersection** (using `std::set_intersection`), and pushes the result back.
    - **OR (`|`)**: Pops the top two result sets, computes their **union** (using `std::set_union`), and pushes the result back.

### Efficiency
By using `std::set_intersection` and `std::set_union` on sorted result sets, the search engine can combine large amounts of data in near-linear time relative to the number of rows.

## Example Walkthrough
Query: `[Vyas & (Rahul | Singh)`

1.  **Lex**: `[Vyas`, `&`, `(`, `Rahul`, `|`, `Singh`, `)`
2.  **Parse (RPN)**: `[Vyas`, `Rahul`, `Singh`, `|`, `&`
3.  **Evaluate**:
    - Push results for `[Vyas` [Stack: S1]
    - Push results for `Rahul` [Stack: S1, S2]
    - Push results for `Singh` [Stack: S1, S2, S3]
    - Pop S3, S2; Compute `S2 | S3` (Union); Push S4 [Stack: S1, S4]
    - Pop S4, S1; Compute `S1 & S4` (Intersection); Push Final Result.
