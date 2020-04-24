#pragma once

#include <vector>
#include <stdint.h>

namespace TokenType
{
    enum
    {
        Return = 128,
        Name,
        Number,
        Invalid,

        Count
    };
}

struct Token
{
    uint32_t type = TokenType::Invalid;

    // Used in error messages
    uint32_t line = 0;
    uint32_t column = 0;
    uint32_t len = 0;
};

void lex(const char* file, std::vector<Token>& tokens);
