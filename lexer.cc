#include "lexer.h"
#include "parser.h"
#include "report_error.h"
#include <cstring>
#include <cassert>

#include <iostream>

void SubString::print() const
{
    for (uint32_t i = 0; i < len; ++i)
    {
        std::cout << start[i];
    }
}

bool SubString::operator==(const SubString& rhs)
{
    if (rhs.len != len) return false;
    for (uint32_t i = 0; i < len; ++i)
    {
        if (rhs.start[i] != start[i]) return false;
    }
    return true;
}

bool operator==(const SubString& lhs, const char* rhs)
{
    uint32_t i = 0;
    for (; rhs[i]; ++i)
    {
        if (i >= lhs.len) return false;
        if (lhs.start[i] != rhs[i]) return false;
    }

    if (i < lhs.len) return false;
    return true;
}

bool operator==(const char* lhs, const SubString& rhs)
{
    return rhs == lhs;
}

static uint64_t string_to_unsigned(const char* start, uint32_t len)
{
    uint64_t result = 0;
    for (uint32_t i = 0; i < len; ++i)
    {
        result *= 10;
        assert(start[i] >= '0' && start[i] <= '9');
        uint8_t digit = start[i] - '0';
        result += digit;
    }
    return result;
}

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
           (c == ',') ||
           (c == '<') ||
           (c == '>') ||
           (c == ';');
}

static bool is_whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
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

static Token get_keyword_token(SubString word)
{
    Token result;
    if (word == "return")
    {
        result.type = TokenType::Return;
    }
    else if (word == "if")
    {
        result.type = TokenType::If;
    }
    else if (word == "else")
    {
        result.type = TokenType::Else;
    }
    else if (word == "while")
    {
        result.type = TokenType::While;
    }
    else if (word == "u8")
    {
        result.type = TokenType::TypeName;
        result.type_id = TypeId::U8;
    }
    else if (word == "u16")
    {
        result.type = TokenType::TypeName;
        result.type_id = TypeId::U16;
    }
    else if (word == "u32")
    {
        result.type = TokenType::TypeName;
        result.type_id = TypeId::U32;
    }
    else if (word == "u64")
    {
        result.type = TokenType::TypeName;
        result.type_id = TypeId::U64;
    }
    else if (word == "i8")
    {
        result.type = TokenType::TypeName;
        result.type_id = TypeId::I8;
    }
    else if (word == "i16")
    {
        result.type = TokenType::TypeName;
        result.type_id = TypeId::I16;
    }
    else if (word == "i32")
    {
        result.type = TokenType::TypeName;
        result.type_id = TypeId::I32;
    }
    else if (word == "i64")
    {
        result.type = TokenType::TypeName;
        result.type_id = TypeId::I64;
    }
    else if (word == "bool")
    {
        result.type = TokenType::TypeName;
        result.type_id = TypeId::Bool;
    }
    else
    {
        result.type = TokenType::Name;
        result.name = word;
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

                compile_assert_with_marker(valid_terminator(file[position]), "Invalid character terminating token", line, identifier_start - line_start, 1);

                Token new_token;
                new_token.type = TokenType::Number,
                new_token.line = line,
                new_token.column = identifier_start - line_start,
                new_token.len = position - identifier_start,
                new_token.number_value = string_to_unsigned(file + identifier_start, new_token.len);

                tokens.push_back(new_token);
            }
            else if (valid_identifier_char(file[position]))
            {
                uint32_t identifier_start = position;
                while (valid_identifier_char(file[position]))
                {
                    ++position;
                }
                
                compile_assert_with_marker(valid_terminator(file[position]), "Invalid character terminating token", line, identifier_start - line_start, 1);

                SubString token_name;
                token_name.start = file + identifier_start;
                token_name.len = position - identifier_start;

                Token new_token = get_keyword_token(token_name);
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
