#include "lexer.h"
#include <cstring>
#include <cassert>

const size_t IDENTIFIER_BUF_SIZE = 64;

static bool valid_identifier_char(char c)
{
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        (c == '_');
}

static bool valid_token_char(char c)
{
    return valid_identifier_char(c) ||
        (c == '(') ||
        (c == ')') ||
        (c == '{') ||
        (c == '}') ||
        (c == '+') ||
        (c == '=') ||
        (c == ':') ||
        (c == ';');
}

static Token single_char_to_token(char c)
{
    Token token;
    switch (c)
    {
        case '(':
        case ')':
        case '{':
        case '}':
        case '+':
        case '=':
        case ':':
        case ';':
            token.type = c;
            break;
        default:
            token.type = TokenType::Invalid;
    }

    return token;
}

static Token get_keyword_token(const char *word)
{
    Token result;
    if (strcmp(word, "return") == 0)
    {
        result.type = TokenType::Return;
    }
    else
    {
        result.type = TokenType::Identifier;
    }

    return result;
}

void lex(const char* file, std::vector<Token>& tokens)
{
    // A token is one of
    //  - A consecutive sequence of letters, digits or underscores
    //  - a single character in (){}+=:;
    uint32_t position = 0;
    while (file[position])
    {
        if (valid_token_char(file[position]))
        {
            if (valid_identifier_char(file[position]))
            {
                uint32_t identifier_start = position;
                while (valid_identifier_char(file[position]))
                {
                    ++position;
                }
                char identifier_buf[IDENTIFIER_BUF_SIZE] = {};
                uint32_t identifier_length = position - identifier_start;
                assert(identifier_length < IDENTIFIER_BUF_SIZE); // ensure 1 extra for terminator
                memcpy(identifier_buf, file + identifier_start, identifier_length);
                
                tokens.push_back(get_keyword_token(identifier_buf));
            }
            else
            {
                // for now, all other tokens have length 1
                tokens.push_back(single_char_to_token(file[position]));
                ++position;
            }
        }
        else
        {
            ++position;
        }
    }
}
