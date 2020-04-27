#pragma once

#include <stdint.h>
#include "lexer.h"

// Keep this pointer alive while calling error reporting functions
void init_error_reporting(const char* file);

void compile_assert_with_marker(bool condition, const char* err_msg, uint32_t line_num, uint32_t col_num, uint32_t marker_len);
void compile_fail_with_marker(const char* err_msg, uint32_t line_num, uint32_t col_num, uint32_t marker_len);

void assert_at_token(bool condition, const char* err_msg, Token token);
void fail_at_token(const char* err_msg, Token token);
