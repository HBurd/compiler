#include "report_error.h"
#include <stdint.h>
#include <iostream>
#include <cstdlib>
#include <cassert>

using std::cout;
using std::endl;

// TODO: This is temporary, obviously we will need multiple files eventually
const char* file_g = nullptr;

void init_error_reporting(const char* file)
{
    file_g = file;
}

static const char* find_line(uint32_t line_num)
{
    uint32_t line_count = 0;
    uint32_t position = 0;
    while (line_count < line_num)
    {
        assert(file_g[position]);

        if (file_g[position] == '\n')
        {
            ++line_count;
        }

        ++position;
    }

    return file_g + position;
}

void compile_assert_with_marker(bool condition, const char* err_msg, uint32_t line_num, uint32_t col_num, uint32_t marker_len)
{
    if (!condition)
    {
        cout << "Line " << line_num + 1 << ": " << err_msg << endl;

        const char* line = find_line(line_num);
        for (uint32_t col = 0; line[col] != '\n'; ++col)
        {
            cout << line[col];
        }

        cout << endl;
        for (uint32_t col = 0; col < col_num; ++col)
        {
            cout << " ";
        }
        
        for (uint32_t i = 0; i < marker_len; ++i)
        {
            cout << "^";
        }
        cout << endl;

        exit(1);
    }
}

void compile_fail_with_marker(const char* err_msg, uint32_t line_num, uint32_t col_num, uint32_t marker_len)
{
    compile_assert_with_marker(false, err_msg, line_num, col_num, marker_len);
}

void assert_at_token(bool condition, const char* err_msg, Token token)
{
    compile_assert_with_marker(condition, err_msg, token.line, token.column, token.len);
}

void fail_at_token(const char* err_msg, Token token)
{
    assert_at_token(false, err_msg, token);
}
