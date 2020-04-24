#include "report_error.h"
#include <stdint.h>
#include <iostream>
#include <cstdlib>

using std::cout;
using std::endl;

// TODO: big waste of space, not scalable
constexpr uint32_t MAX_LINE_COUNT = 1 << 20;

// TODO: This is temporary, obviously we will need multiple files eventually
const char* file_g = nullptr;
const char* line_index_g[MAX_LINE_COUNT];

void init_error_reporting(const char* file) {
    file_g = file;
    
    // keep track of the start of each line
    line_index_g[0] = 0;

    uint32_t file_marker = 0;
    uint32_t line = 0;
    while(file_g[file_marker]) {
        if (file_g[file_marker] == '\n') {
            ++line;
            line_index_g[line] = file_g + file_marker + 1;
        }
        ++file_marker;
    }
}

void compile_assert_with_marker(bool condition, const char* err_msg, uint32_t line_num, uint32_t col_num, uint32_t marker_len) {
    if (!condition) {
        cout << "Line " << line_num + 1 << ": " << err_msg << endl;

        const char* line = line_index_g[line_num];
        for (uint32_t col = 0; line[col] != '\n'; ++col) {
            cout << line[col];
        }

        cout << endl;
        for (uint32_t col = 0; col < col_num; ++col) {
            cout << " ";
        }
        
        for (uint32_t i = 0; i < marker_len; ++i) {
            cout << "^";
        }
        cout << endl;

        exit(1);
    }
}

void compile_fail_with_marker(const char* err_msg, uint32_t line_num, uint32_t col_num, uint32_t marker_len) {
    compile_assert_with_marker(false, err_msg, line_num, col_num, marker_len);
}

void parse_assert(bool condition, const char* err_msg, Token token) {
    compile_assert_with_marker(condition, err_msg, token.line, token.column, token.len);
}

void parse_fail(const char* err_msg, Token token) {
    parse_assert(false, err_msg, token);
}
