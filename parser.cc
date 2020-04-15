#include "parser.h"
#include <iostream>
#include <cassert>

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

static uint32_t parse_expression(TokenSlice tokens) {
    uint32_t token_idx = 0;
    while (tokens[token_idx].type != ';') {
        ++token_idx;
    }

    std::cout << "expression" << std::endl;
    
    // does not include semicolon
    return token_idx;
}

static uint32_t parse_statement(TokenSlice tokens) {
    assert(tokens.length >= 3); // at least 2 tokens and a semicolon

    uint32_t token_idx = 0;
    if (tokens[0].type == TokenType::Identifier) {
        if (tokens[1].type == ':') {
            token_idx += parse_def(tokens);
        }
        else if (tokens[1].type == '=') {
            std::cout << "assignment" << std::endl;
            token_idx += 2 + parse_expression(tokens.from(2));
        }
    }
    else if (tokens[0].type == TokenType::Return) {
        std::cout << "return" << std::endl;
        token_idx += 1 + parse_expression(tokens.from(1));
    }
    else {
        assert(false);
    }

    assert(tokens[token_idx].type == ';');
    return token_idx + 1;
}

static uint32_t parse_statement_list(TokenSlice tokens) {
    assert(tokens.length >= 2); // shortest is {}
    assert(tokens[0].type == '{');
    
    uint32_t token_idx = 1;
    while (tokens[token_idx].type != '}') {
        std::cout << "statement" << std::endl;
        token_idx += parse_statement(tokens.from(token_idx));
    }

    return token_idx + 1;
}

constexpr uint32_t MIN_DEF_LEN = 6; // identifier: () {}

static uint32_t parse_def(TokenSlice tokens) {
    // figure out which type of def:
    
    assert(tokens.length >= MIN_DEF_LEN);

    assert(tokens[0].type == TokenType::Identifier);
    assert(tokens[1].type == ':');

    if (tokens[2].type == '(') {
        // this is a function def
        std::cout << "function def" << std::endl;
        assert(tokens[3].type == ')' && tokens[4].type == '{');
        return 4 + parse_statement_list(tokens.from(4));
    }
    else if (tokens[2].type == '=') {
        // this is a variable / constant def
        std::cout << "variable def!!" << std::endl;
        return 3 + parse_expression(tokens.from(3));
    }
    else {
        assert(false);
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
                token_idx += parse_def(token_slice.from(token_idx));
            } break;
            default:
                assert(false);
        }
    }
}
