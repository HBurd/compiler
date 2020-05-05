#include "parser.h"
#include "util.h"
#include "report_error.h"

#include <iostream>
#include <cassert>
#include <cstdlib>
#include <cstring>

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

ASTNode* AST::push(ASTNode* node)
{
    uint32_t align;
    uint32_t size;
    switch (node->type)
    {
        case ASTNodeType::BinaryOperator:
            align = alignof(ASTBinOpNode);
            size = sizeof(ASTBinOpNode);
            break;
        case ASTNodeType::FunctionDef:
        case ASTNodeType::VariableDef:
        case ASTNodeType::Assignment:
        case ASTNodeType::Identifier:
            align = alignof(ASTIdentifierNode);
            size = sizeof(ASTIdentifierNode);
            break;
        case ASTNodeType::Number:
            align = alignof(ASTNumberNode);
            size = sizeof(ASTNumberNode);
            break;
        case ASTNodeType::StatementList:
            align = alignof(ASTStatementListNode);
            size = sizeof(ASTStatementListNode);
            break;
        default:
            align = alignof(ASTNode);
            size = sizeof(ASTNode);
    }

    bool set_start = (next == 0);

    // Get alignment right
    next += (align - (next % align)) % align;
    ASTNode* result = (ASTNode*)(data + next);

    if (set_start) start = result;

    assert(next + size <= MAX_SIZE);

    memcpy(result, node, size);
    next += size;
    return result;
}

static bool lookup_symbol(Array<SymbolData>& symbol_table, SubString name, uint32_t* index) {
    for (uint32_t i = 0; i < symbol_table.length; ++i) {
        if (symbol_table[i].name == name) {
            if (index) {
                *index = i;
            }
            return true;
        }
    }
    return false;
}

// ----------------------------
// Functions for generating AST
// ----------------------------

static uint32_t parse_def(TokenSlice tokens, AST& ast, Array<SymbolData>& symbols, ASTNode** node);

// We stick expressions on the AST in RPN to make left to right precedence easier
static uint32_t parse_expression(TokenSlice tokens, AST& ast, Array<SymbolData>& symbols, uint8_t precedence, ASTNode** expr) {

    uint32_t token_idx = 0;

    if (tokens[0].type == '(') {
        ++token_idx;

        ASTNode* subexpr = nullptr;
        token_idx += parse_expression(tokens.from(token_idx), ast, symbols, 1, &subexpr);
        assert_at_token(subexpr, "Empty subexpression", tokens[token_idx]);
        
        *expr = subexpr;
        assert_at_token(tokens[token_idx].type == ')', "Missing ')'", tokens[token_idx]);
        ++token_idx;
    }
    else if (tokens[0].type == TokenType::Name || tokens[0].type == TokenType::Number) {
        // recurse until precedence matches that of the next operator
        if (OPERATOR_PRECEDENCE[tokens[token_idx + 1].type] >= precedence) {
            // this sticks the subexpr (of higher precedence) onto the AST
            token_idx += parse_expression(tokens, ast, symbols, precedence + 1, expr);

            // now token_idx is after a precedence + 1 subexpression

            assert(OPERATOR_PRECEDENCE[tokens[token_idx].type] <= precedence);

            while (OPERATOR_PRECEDENCE[tokens[token_idx].type] == precedence) {
                uint32_t op = tokens[token_idx].type;
                ++token_idx;

                // now recurse so that the right expression is on the AST
                ASTNode* right_subexpr;
                token_idx += parse_expression(tokens.from(token_idx), ast, symbols, precedence + 1, &right_subexpr);

                (*expr)->sibling = right_subexpr;

                ASTBinOpNode op_node(op);
                op_node.child = *expr;

                *expr = ast.push(&op_node);
            }
        }
        else {
            switch (tokens[0].type)
            {
                case TokenType::Name:
                {
                    uint32_t symbol_id;
                    assert_at_token(lookup_symbol(symbols, tokens[0].name, &symbol_id), "Unknown identifier", tokens[0]);

                    ASTIdentifierNode new_node(ASTNodeType::Identifier, symbol_id);
                    *expr = ast.push(&new_node);
                } break;
                case TokenType::Number:
                {
                    ASTNumberNode new_node(tokens[0].number_value);
                    *expr = ast.push(&new_node);
                } break;
                default:
                    assert(false);
            }
            ++token_idx;
        }
    }
    else {
        fail_at_token("Invalid expression", tokens[0]);
    }

    assert_at_token(
        OPERATOR_PRECEDENCE[tokens[token_idx].type] || tokens[token_idx].type == ';' || tokens[token_idx].type == ')',
        "Unknown token terminating expression",
        tokens[token_idx]);

    assert(OPERATOR_PRECEDENCE[tokens[token_idx].type] < precedence);
    return token_idx;
}

static uint32_t parse_statement(TokenSlice tokens, AST& ast, Array<SymbolData>& symbols, ASTNode** node) {
    assert_at_token(tokens.length >= 3, "Invalid statement", tokens[0]); // at least 2 tokens and a semicolon

    if (tokens[0].type == TokenType::Name) {
        if (tokens[1].type == ':') {
            // Parse declaration
            uint32_t parse_length = parse_def(tokens, ast, symbols, node);
            return parse_length;
        }
        else if (tokens[1].type == '=') {
            // Parse assignment
            uint32_t symbol_id;
            bool found_symbol = lookup_symbol(symbols, tokens[0].name, &symbol_id);
            assert_at_token(found_symbol, "Unknown symbol", tokens[0]);

            ASTIdentifierNode new_node(ASTNodeType::Assignment, symbol_id);
            *node = ast.push(&new_node);

            uint32_t parse_length = 1 + parse_expression(tokens.from(2), ast, symbols, 1, &(*node)->child);
            return 2 + parse_length;
        }
    }
    else if (tokens[0].type == TokenType::Return) {
        ASTNode new_node(ASTNodeType::Return);
        *node = ast.push(&new_node); // TODO: can only return an expression right now
        uint32_t parse_length = 1 + parse_expression(tokens.from(1), ast, symbols, 1, &(*node)->child);
        return 1 + parse_length;
    }

    fail_at_token("Invalid statement", tokens[0]);

    return 0;
}

static uint32_t parse_statement_list(TokenSlice tokens, AST& ast, Array<SymbolData>& symbols, ASTNode** node) {
    assert_at_token(tokens.length >= 2, "Invalid statement list", tokens[0]); // shortest is {}

    assert_at_token(tokens[0].type == '{', "Expected '{'", tokens[0]);

    ASTStatementListNode new_statement_list_node(symbols);
    *node = ast.push(&new_statement_list_node);    

    ASTStatementListNode* statement_list_node = static_cast<ASTStatementListNode*>(*node);

    ASTNode* last_node = *node;

    uint32_t token_idx = 1;
    while (token_idx < tokens.length && tokens[token_idx].type != '}') {
        uint32_t parse_length = parse_statement(tokens.from(token_idx), ast, statement_list_node->symbols, &last_node->sibling);

        token_idx += parse_length;
        last_node = last_node->sibling;
    }

    // fix up the statement list node
    statement_list_node->child = (*node)->sibling;
    statement_list_node->sibling = nullptr;

    return token_idx + 1;
}

constexpr uint32_t MIN_DEF_LEN = 6; // identifier: () {}

static uint32_t parse_def(TokenSlice tokens, AST& ast, Array<SymbolData>& symbols, ASTNode** node) {

    assert_at_token(
        tokens.length >= MIN_DEF_LEN && tokens[0].type == TokenType::Name && tokens[1].type == ':',
        "Invalid definition",
        tokens[0]);

    // check if an entry is already in the symbol table
    assert_at_token(!lookup_symbol(symbols, tokens[0].name, nullptr), "Symbol already declared", tokens[0]);

    uint32_t new_symbol_id = symbols.length;

    SymbolData new_symbol;
    new_symbol.name = tokens[0].name;
    symbols.push(new_symbol);

    // figure out which type of def:
    if (tokens[2].type == '(') {
        // this is a function def
        assert_at_token(tokens[3].type == ')', "Invalid parameter list", tokens[3]);

        ASTIdentifierNode function_identifier_node(ASTNodeType::FunctionDef, new_symbol_id);
        *node = ast.push(&function_identifier_node);

        ASTNode parameter_list_node(ASTNodeType::ParameterList);
        (*node)->child = ast.push(&parameter_list_node);

        // Need to make the function's symbol table here so we can add the parameters to the scope
        Array<SymbolData> function_symbols;
        function_symbols.max_length = MAX_SYMBOLS;
        function_symbols.data = new SymbolData[MAX_SYMBOLS];

        uint32_t parsed_length = parse_statement_list(tokens.from(4), ast, function_symbols, &(*node)->child->sibling);

        return 4 + parsed_length;
    }
    else if (tokens[2].type == '=') {
        // this is a variable / constant def
        ASTIdentifierNode new_node(ASTNodeType::VariableDef, new_symbol_id);
        *node = ast.push(&new_node);

        uint32_t parsed_length = 1 + parse_expression(tokens.from(3), ast, symbols, 1, &(*node)->child);

        return 3 + parsed_length;
    }
    else {
        fail_at_token("Invalid definition", tokens[0]);
        return 0;
    }
}

static void print_ast_node(ASTNode* node, Array<SymbolData> symbols, uint32_t depth) {

    for (uint32_t i = 0; i < depth; ++i) {
        std::cout << "  ";
    }

    if (node->type == ASTNodeType::BinaryOperator) {
        std::cout << static_cast<ASTBinOpNode*>(node)->op << std::endl;
    }
    else if (node->type == ASTNodeType::Number) {
        std::cout << static_cast<ASTNumberNode*>(node)->value << std::endl;
    }
    else if (node->type == ASTNodeType::Identifier) {
        uint32_t symbol_id = static_cast<ASTIdentifierNode*>(node)->symbol_id;
        symbols[symbol_id].name.print();
        std::cout << std::endl;
    }
    else if (node->type == ASTNodeType::Assignment) {
        std::cout << "Assignment(";
        uint32_t symbol_id = static_cast<ASTIdentifierNode*>(node)->symbol_id;
        symbols[symbol_id].name.print();
        std::cout << ")" << std::endl;
    }
    else if (node->type == ASTNodeType::VariableDef) {
        std::cout << "VariableDef(";
        uint32_t symbol_id = static_cast<ASTIdentifierNode*>(node)->symbol_id;
        symbols[symbol_id].name.print();
        std::cout << ")" << std::endl;
    }
    else {
        std::cout << AST_NODE_TYPE_NAME[node->type] << std::endl;
    }

    if (node->type == ASTNodeType::StatementList) {
        symbols = static_cast<ASTStatementListNode*>(node)->symbols;
    }

    ASTNode* child = node->child;
    while (child) {
        print_ast_node(child, symbols, depth + 1);
        child = child->sibling;
    }
}

void parse(const std::vector<Token>& tokens, AST& ast, Array<SymbolData>& global_symbols) {

    uint32_t token_idx = 0;

    TokenSlice token_slice;
    token_slice.data = tokens.data();
    token_slice.length = tokens.size();

    while (token_idx < token_slice.length) {
        // top level expressions
        switch (token_slice[token_idx].type) {
            case TokenType::Name: {
                ASTNode* node;
                uint32_t parsed_length = parse_def(token_slice.from(token_idx), ast, global_symbols, &node);

                token_idx += parsed_length;
            } break;
            default:
                fail_at_token("Invalid top-level statement", token_slice[token_idx]);
        }
    }

    print_ast_node(ast.start, global_symbols, 0);
}
