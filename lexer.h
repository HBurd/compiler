#pragma once

#include "util.h"
#include <vector>
#include <stdint.h>

namespace TokenType
{
    enum
    {
        Return = 128,
        Name,
        TypeName,
        Number, 
        If,
        Else,
        While,
        String,
        
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

    union
    {
        uint64_t number_value;
        SubString str;
        uint32_t type_id;       // for default type names, like u32 etc
    };

    Token()
    {
        // hack to un-delete default token constructor
        str = SubString();
    }
};

void lex(const char* file, std::vector<Token>& tokens);
