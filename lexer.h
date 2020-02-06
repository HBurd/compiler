#pragma once

#include <vector>
#include <stdint.h>

enum class TokenType
{
    Invalid,
    OpenParen,
    CloseParen,
    OpenBrace,
    CloseBrace,
    Plus,
    Equals,
    Colon,
    Semicolon,
    Return,
    Identifier
};

struct Token
{
    TokenType type = TokenType::Invalid;
    union
    {
        uint32_t identifier_id = 0;
    };

    const char* to_string();
};

void lex(const char* file, std::vector<Token>& tokens);
