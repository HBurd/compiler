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
    };
}

struct Token
{
    uint32_t type = TokenType::Invalid;
    union
    {
        uint32_t identifier_id = 0;
    };
};

void lex(const char* file, std::vector<Token>& tokens);
