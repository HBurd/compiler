#pragma once

#include <stdint.h>
#include "lexer.h"

// Keep this pointer alive while calling error reporting functions
void init_error_reporting(const char* file);

void parse_assert(bool condition, const char* err_msg, Token token);
void parse_fail(const char* err_msg, Token token);
