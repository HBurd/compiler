#include "parser.h"
#include <iostream>
#include <cassert>
#include <cstdlib>

struct TokenSlice {
    Token const * data = nullptr;
    uint32_t length = 0;

    TokenSlice from(uint32_t idx) {
        assert(idx < length);
        TokenSlice new_slice;
        new_slice.data = data + idx;
        new_slice.length = length - idx;
        return new_slice;
    }

    Token operator[](uint32_t idx) {
        return data[idx];
    }
};

static uint32_t parse_def(TokenSlice tokens);

static void compile_assert(int condition, const char* err_msg, uint32_t line) {
    if (!condition) {
        std::cout << "Line " << line << ": " << err_msg << std::endl;
        exit(1);
    }
}

static void compile_fail(const char* err_msg, uint32_t line) {
    compile_assert(false, err_msg, line);
}

static uint32_t parse_expression(TokenSlice tokens) {
    uint32_t token_idx = 0;
    while (tokens[token_idx].type != ';') {
        ++token_idx;
    }
    
    // does not include semicolon
    return token_idx;
}

static uint32_t parse_statement(TokenSlice tokens) {
    if (tokens.length < 3) { // at least 2 tokens and a semicolon
        return 0;
    }

    if (tokens[0].type == TokenType::Identifier) {
        if (tokens[1].type == ':') {
            return 1 + parse_def(tokens);
        }
        else if (tokens[1].type == '=') {
            uint32_t parse_length = parse_expression(tokens.from(2));
            compile_assert(parse_length, "Invalid expression on rhs of assignment", tokens[1].line);
            return 3 + parse_length;
        }
    }
    else if (tokens[0].type == TokenType::Return) {
        uint32_t parse_length = parse_expression(tokens.from(1));
        compile_assert(parse_length, "Invalid expression after return", tokens[1].line);
        return 2 + parse_length;
    }

    return 0;
}

static uint32_t parse_statement_list(TokenSlice tokens) {
    if (tokens.length < 2) { // shortest is {}
        return 0;
    }

    compile_assert(tokens[0].type == '{', "Expected '{'", tokens[0].line);
    
    uint32_t token_idx = 1;
    while (token_idx < tokens.length && tokens[token_idx].type != '}') {
        uint32_t parse_length = parse_statement(tokens.from(token_idx));
        compile_assert(parse_length, "Invalid statement", tokens[token_idx].line);
        token_idx += parse_length;
    }

    return token_idx + 1;
}

constexpr uint32_t MIN_DEF_LEN = 6; // identifier: () {}

static uint32_t parse_def(TokenSlice tokens) {
    if (tokens.length < MIN_DEF_LEN) {
        return 0;
    }

    // this one is an actual assert - parse_def has to be called this way!
    assert(tokens[0].type == TokenType::Identifier);

    if (tokens[1].type != ':') {
        return 0;
    }

    // figure out which type of def:
    if (tokens[2].type == '(') {
        // this is a function def
        compile_assert(tokens[3].type == ')', "Invalid parameter list", tokens[3].line);

        uint32_t parsed_length = parse_statement_list(tokens.from(4));
        compile_assert(parsed_length, "Invalid function body", tokens[3].line);

        return 4 + parsed_length;
    }
    else if (tokens[2].type == '=') {
        // this is a variable / constant def
        uint32_t parsed_length = parse_expression(tokens.from(3));
        compile_assert(parsed_length, "Invalid expression on rhs of variable definition", tokens[2].line);

        return 3 + parsed_length;
    }
    else {
        return 0;
    }
}

void parse(const std::vector<Token>& tokens) {
    uint32_t token_idx = 0;

    TokenSlice token_slice;
    token_slice.data = tokens.data();
    token_slice.length = tokens.size();

    while (token_idx < token_slice.length) {
        // top level expressions
        switch (token_slice[token_idx].type) {
            case TokenType::Identifier: {
                uint32_t parsed_length = parse_def(token_slice.from(token_idx));
                compile_assert(parsed_length, "Invalid definition", token_slice[token_idx].line);
                token_idx += parsed_length;
            } break;
            default:
                compile_fail("Invalid top-level statement", token_slice[token_idx].line);
        }
    }
}
