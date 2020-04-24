#include "lexer.h"
#include "report_error.h"
#include <cstring>
#include <cassert>

const size_t IDENTIFIER_BUF_SIZE = 64;

static bool is_number(char c)
{
    return c >= '0' && c <= '9';
}

// TODO: This may be misleading when we have e.g. < and <<
static bool is_single_char_token(char c)
{
    return (c == '(') ||
           (c == ')') ||
           (c == '{') ||
           (c == '}') ||
           (c == '+') ||
           (c == '-') ||
           (c == '*') ||
           (c == '=') ||
           (c == ':') ||
           (c == ';');
}

static bool is_whitespace(char c)
{
    return c == ' ' || c == '\t';
}

static bool valid_identifier_char(char c)
{
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        (c == '_');
}

static bool valid_token_char(char c)
{
    return valid_identifier_char(c) || is_single_char_token(c);
}

static bool valid_terminator(char c)
{
    return is_single_char_token(c) || is_whitespace(c);
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
        result.type = TokenType::Name;
    }

    return result;
}

void lex(const char* file, std::vector<Token>& tokens)
{
    uint32_t position = 0;
    uint32_t line = 0;
    uint32_t line_start = 0;
    while (file[position])
    {
        if (file[position] == '\n')
        {
            ++line;
            ++position;
            line_start = position;
        }
        else if (valid_token_char(file[position]))
        {
            if (is_number(file[position]))
            {
                uint32_t identifier_start = position;
                while (is_number(file[position]))
                {
                    ++position;
                }

                compile_assert_with_marker(valid_terminator(file[position]), "Invalid character terminating token", line, line_start - position, 1);

                Token new_token;
                new_token.type = TokenType::Number,
                new_token.line = line,
                new_token.column = identifier_start - line_start,
                new_token.len = position - identifier_start,

                tokens.push_back(new_token);
            }
            else if (valid_identifier_char(file[position]))
            {
                uint32_t identifier_start = position;
                while (valid_identifier_char(file[position]))
                {
                    ++position;
                }
                
                compile_assert_with_marker(valid_terminator(file[position]), "Invalid character terminating token", line, line_start - position, 1);

                char identifier_buf[IDENTIFIER_BUF_SIZE] = {};
                uint32_t identifier_length = position - identifier_start;
                assert(identifier_length < IDENTIFIER_BUF_SIZE); // ensure 1 extra for terminator
                memcpy(identifier_buf, file + identifier_start, identifier_length);

                Token new_token = get_keyword_token(identifier_buf);
                new_token.line = line;
                new_token.column = identifier_start - line_start;
                new_token.len = position - identifier_start;
                
                tokens.push_back(new_token);
            }
            else if (is_single_char_token(file[position]))
            {
                Token new_token;
                new_token.type = file[position];
                new_token.line = line;
                new_token.column = position - line_start;
                new_token.len = 1;

                tokens.push_back(new_token);
                ++position;
            }
            else
            {
                compile_fail_with_marker("Invalid character", line, position - line_start, 1);
            }
        }
        else
        {
            ++position;
        }
    }
}
