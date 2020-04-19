#pragma once

#include <vector>
#include <stdint.h>

namespace TokenType
{
    enum
    {
        Return = 128,
        Identifier,
        Invalid,

        Count
    };
}

struct Token
{
    uint32_t type = TokenType::Invalid;
    uint32_t line = 0;
};

void lex(const char* file, std::vector<Token>& tokens);
