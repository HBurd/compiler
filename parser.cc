#include "parser.h"
#include "util.h"
#include "report_error.h"

#include <iostream>
#include <cassert>
#include <cstdlib>

const char* AST_NODE_TYPE_NAME[] = {
    [ASTNodeType::Invalid] = "Invalid",
    [ASTNodeType::FunctionDef] = "FunctionDef",
    [ASTNodeType::ParameterList] = "ParameterList",
    [ASTNodeType::StatementList] = "StatementList",
    [ASTNodeType::VariableDef] = "VariableDef",
    [ASTNodeType::Assignment] = "Assignment",
    [ASTNodeType::Return] = "Return",
    [ASTNodeType::Identifier] = "Identifier",
    [ASTNodeType::Number] = "Number",
    [ASTNodeType::BinaryOperator] = "BinaryOperator",
};

uint8_t OPERATOR_PRECEDENCE[] = {
    ['*'] = 2,
    ['+'] = 1,
    ['-'] = 1,  // TODO: how to differentiate unary and binary
};

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

// TODO: there must be a better way to structure this so I don't need to traverse the list for this
static void set_next(ASTNode* node, ASTNode* next) {
    while(node->next) {
        node = node->next;
    }
    node->next = next;
}

// ----------------------------
// Functions for generating AST
// ----------------------------

static uint32_t parse_def(TokenSlice tokens, Array<ASTNode, MAX_AST_SIZE>* ast, ASTNode** node);

// We stick expressions on the AST in RPN to make left to right precedence easier
static uint32_t parse_expression(TokenSlice tokens, Array<ASTNode, MAX_AST_SIZE>* ast, uint8_t precedence, ASTNode** expr) {

    uint32_t token_idx = 0;

    if (tokens[0].type == '(') {
        ++token_idx;

        ASTNode* subexpr = nullptr;
        token_idx += parse_expression(tokens.from(token_idx), ast, 1, &subexpr);
        parse_assert(subexpr, "Empty subexpression", tokens[token_idx]);
        
        *expr = subexpr;
        parse_assert(tokens[token_idx].type == ')', "Missing ')'", tokens[token_idx]);
        ++token_idx;
    }
    else if (tokens[0].type == TokenType::Name || tokens[0].type == TokenType::Number) {
        // recurse until precedence matches that of the next operator
        if (OPERATOR_PRECEDENCE[tokens[token_idx + 1].type] >= precedence) {
            // this sticks the subexpr (of higher precedence) onto the AST
            token_idx += parse_expression(tokens, ast, precedence + 1, expr);

            // now token_idx is after a precedence + 1 subexpression

            assert(OPERATOR_PRECEDENCE[tokens[token_idx].type] <= precedence);

            while (OPERATOR_PRECEDENCE[tokens[token_idx].type] == precedence) {
                uint32_t op = tokens[token_idx].type;
                ++token_idx;

                // now recurse so that the right expression is on the AST
                ASTNode* right_subexpr;
                token_idx += parse_expression(tokens.from(token_idx), ast, precedence + 1, &right_subexpr);

                set_next(*expr, right_subexpr);

                ASTNode op_node{ASTNodeType::BinaryOperator};
                op_node.op = op;
                op_node.children = 2;
                op_node.next = *expr;
                *expr = &ast->push(op_node);
            }
        }
        else {
            switch (tokens[0].type)
            {
                case TokenType::Name:
                    *expr = &ast->push(ASTNode{ASTNodeType::Identifier});
                    break;
                case TokenType::Number:
                    *expr = &ast->push(ASTNode{ASTNodeType::Number});
                    break;
                default:
                    assert(false);
            }
            ++token_idx;
        }
    }
    else {
        parse_fail("Invalid expression", tokens[0]);
    }

    parse_assert(
        OPERATOR_PRECEDENCE[tokens[token_idx].type] || tokens[token_idx].type == ';' || tokens[token_idx].type == ')',
        "Unknown token terminating expression",
        tokens[token_idx]);

    assert(OPERATOR_PRECEDENCE[tokens[token_idx].type] < precedence);
    return token_idx;
}

static uint32_t parse_statement(TokenSlice tokens, Array<ASTNode, MAX_AST_SIZE>* ast, ASTNode** node) {
    if (tokens.length < 3) { // at least 2 tokens and a semicolon
        return 0;
    }

    if (tokens[0].type == TokenType::Name) {
        if (tokens[1].type == ':') {
            uint32_t parse_length = parse_def(tokens, ast, node);
            parse_assert(parse_length, "Invalid definition", tokens[0]);
            return parse_length;
        }
        else if (tokens[1].type == '=') {
            *node = &ast->push(ASTNode{ASTNodeType::Assignment});
            (*node)->children = 1;
            uint32_t parse_length = 1 + parse_expression(tokens.from(2), ast, 1, &(*node)->next);
            parse_assert(parse_length, "Invalid expression on rhs of assignment", tokens[1]);
            return 2 + parse_length;
        }
    }
    else if (tokens[0].type == TokenType::Return) {
        *node = &ast->push(ASTNode{ASTNodeType::Return}); // TODO: can only return an expression right now
        (*node)->children = 1;
        uint32_t parse_length = 1 + parse_expression(tokens.from(1), ast, 1, &(*node)->next);
        parse_assert(parse_length, "Invalid expression after return", tokens[1]);
        return 1 + parse_length;
    }

    std::cout << "Invalid statement starting with token id " << tokens[0].type << ", line " << tokens[0].line << std::endl;

    return 0;
}

static uint32_t parse_statement_list(TokenSlice tokens, Array<ASTNode, MAX_AST_SIZE>* ast, ASTNode** node) {
    if (tokens.length < 2) { // shortest is {}
        return 0;
    }

    parse_assert(tokens[0].type == '{', "Expected '{'", tokens[0]);

    *node = &ast->push(ASTNode{ASTNodeType::StatementList});    

    // use this to set the 'next' field for each statement
    ASTNode* last_node = *node;

    uint32_t token_idx = 1;
    while (token_idx < tokens.length && tokens[token_idx].type != '}') {
        ASTNode* next_node;
        uint32_t parse_length = parse_statement(tokens.from(token_idx), ast, &next_node);
        parse_assert(parse_length, "Invalid statement", tokens[token_idx]);

        set_next(last_node, next_node);
        ++(*node)->children;
        token_idx += parse_length;
    }

    return token_idx + 1;
}

constexpr uint32_t MIN_DEF_LEN = 6; // identifier: () {}

static uint32_t parse_def(TokenSlice tokens, Array<ASTNode, MAX_AST_SIZE>* ast, ASTNode** node) {
    if (tokens.length < MIN_DEF_LEN) {
        return 0;
    }

    // this one is an actual assert - parse_def has to be called this way!
    assert(tokens[0].type == TokenType::Name);

    if (tokens[1].type != ':') {
        return 0;
    }

    // figure out which type of def:
    if (tokens[2].type == '(') {
        // this is a function def
        parse_assert(tokens[3].type == ')', "Invalid parameter list", tokens[3]);

        *node = &ast->push(ASTNode{ASTNodeType::FunctionDef});
        (*node)->children = 2;

        (*node)->next = &ast->push(ASTNode{ASTNodeType::ParameterList});

        uint32_t parsed_length = parse_statement_list(tokens.from(4), ast, &(*node)->next->next);
        parse_assert(parsed_length, "Invalid function body", tokens[3]);

        return 4 + parsed_length;
    }
    else if (tokens[2].type == '=') {
        // this is a variable / constant def
        *node = &ast->push(ASTNode{ASTNodeType::VariableDef});
        (*node)->children = 1;

        uint32_t parsed_length = 1 + parse_expression(tokens.from(3), ast, 1, &(*node)->next);
        parse_assert(parsed_length, "Invalid expression on rhs of variable definition", tokens[2]);

        return 3 + parsed_length;
    }
    else {
        return 0;
    }
}

ASTNode* print_ast_node(ASTNode* node, uint32_t depth) {

    for (uint32_t i = 0; i < depth; ++i) {
        std::cout << "  ";
    }

    if (node->type == ASTNodeType::BinaryOperator) {
        std::cout << node->op << std::endl;
    }
    else {
        std::cout << AST_NODE_TYPE_NAME[node->type] << std::endl;
    }

    ASTNode* child = node->next;
    for (uint32_t i = 0; i < node->children; ++i) {
        child = print_ast_node(child, depth + 1);
    }
    return child;
}

Array<ASTNode, MAX_AST_SIZE>* parse(const std::vector<Token>& tokens) {
    Array<ASTNode, MAX_AST_SIZE>* ast = new Array<ASTNode, MAX_AST_SIZE>;

    uint32_t token_idx = 0;

    TokenSlice token_slice;
    token_slice.data = tokens.data();
    token_slice.length = tokens.size();

    while (token_idx < token_slice.length) {
        // top level expressions
        switch (token_slice[token_idx].type) {
            case TokenType::Name: {
                ASTNode* node;
                uint32_t parsed_length = parse_def(token_slice.from(token_idx), ast, &node);
                parse_assert(parsed_length != 0, "Invalid definition", token_slice[token_idx]);

                token_idx += parsed_length;
            } break;
            default:
                parse_fail("Invalid top-level statement", token_slice[token_idx]);
        }
    }

    // verify structure of the AST by counting nodes 2 ways
    ASTNode* node = &(ast->data[0]);
    uint32_t node_count = 0;
    while (node) {
        ++node_count;
        node = node->next;
    }

    assert(node_count == ast->size);

    print_ast_node(&((*ast)[0]), 0);

    return ast;
}
