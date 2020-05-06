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

ASTNode* AST::push_orphan(const ASTNode& node)
{
    uint32_t align;
    uint32_t size;
    switch (node.type)
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

    // Get alignment right
    next += (align - (next % align)) % align;
    ASTNode* result = (ASTNode*)(data + next);

    assert(next + size <= MAX_SIZE);

    memcpy(result, &node, size);
    next += size;

    return result;
}

ASTNode* AST::push(const ASTNode& node)
{
    ASTNode* result = push_orphan(node);
    *next_node_ref = result;
    next_node_ref = &result->sibling;

    return result;
}

void AST::begin_children(ASTNode* node)
{
    next_node_ref = &node->child;
}

void AST::end_children(ASTNode* node)
{
    next_node_ref = &node->sibling;
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

static uint32_t parse_def(TokenSlice tokens, AST& ast, Array<SymbolData>& symbols);

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

                ASTNode* op_node = ast.push_orphan(ASTBinOpNode(op));
                op_node->child = *expr;
                *expr = op_node;
            }
        }
        else {
            switch (tokens[0].type)
            {
                case TokenType::Name:
                    uint32_t symbol_id;
                    assert_at_token(lookup_symbol(symbols, tokens[0].name, &symbol_id), "Unknown identifier", tokens[0]);

                    *expr = ast.push_orphan(ASTIdentifierNode(ASTNodeType::Identifier, symbol_id));
                    break;
                case TokenType::Number:
                    *expr = ast.push_orphan(ASTNumberNode(tokens[0].number_value));
                    break;
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

static uint32_t parse_statement(TokenSlice tokens, AST& ast, Array<SymbolData>& symbols) {
    assert_at_token(tokens.length >= 3, "Invalid statement", tokens[0]); // at least 2 tokens and a semicolon

    if (tokens[0].type == TokenType::Name) {
        if (tokens[1].type == ':') {
            // Parse declaration
            uint32_t parse_length = parse_def(tokens, ast, symbols);
            return parse_length;
        }
        else if (tokens[1].type == '=') {
            // Parse assignment
            uint32_t symbol_id;
            bool found_symbol = lookup_symbol(symbols, tokens[0].name, &symbol_id);
            assert_at_token(found_symbol, "Unknown symbol", tokens[0]);

            ASTNode* assign_node = ast.push(ASTIdentifierNode(ASTNodeType::Assignment, symbol_id));

            uint32_t parse_length = 1 + parse_expression(tokens.from(2), ast, symbols, 1, &assign_node->child);
            return 2 + parse_length;
        }
    }
    else if (tokens[0].type == TokenType::Return) {
        ASTNode* return_node = ast.push(ASTNode(ASTNodeType::Return)); // TODO: can only return an expression right now
        uint32_t parse_length = 1 + parse_expression(tokens.from(1), ast, symbols, 1, &return_node->child);
        return 1 + parse_length;
    }

    fail_at_token("Invalid statement", tokens[0]);

    return 0;
}

static uint32_t parse_statement_list(TokenSlice tokens, AST& ast, Array<SymbolData>& symbols) {
    assert_at_token(tokens.length >= 2, "Invalid statement list", tokens[0]); // shortest is {}

    assert_at_token(tokens[0].type == '{', "Expected '{'", tokens[0]);

    ASTNode* statement_list_node = ast.push(ASTStatementListNode(symbols));

    ast.begin_children(statement_list_node);

    uint32_t token_idx = 1;
    while (token_idx < tokens.length && tokens[token_idx].type != '}') {
        uint32_t parse_length = parse_statement(tokens.from(token_idx), ast, static_cast<ASTStatementListNode*>(statement_list_node)->symbols);

        token_idx += parse_length;
    }

    ast.end_children(statement_list_node);

    return token_idx + 1;
}

// TODO: seems messy / unnecessary
constexpr uint32_t MIN_DEF_LEN = 6; // identifier: () {}

static uint32_t parse_def(TokenSlice tokens, AST& ast, Array<SymbolData>& symbols) {

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

        ASTNode* function_identifier_node = ast.push(ASTIdentifierNode(ASTNodeType::FunctionDef, new_symbol_id));

        ast.begin_children(function_identifier_node);

        ast.push(ASTNode(ASTNodeType::ParameterList));

        // Need to make the function's symbol table here so we can add the parameters to the scope
        Array<SymbolData> function_symbols;
        function_symbols.max_length = MAX_SYMBOLS;
        function_symbols.data = new SymbolData[MAX_SYMBOLS];

        uint32_t parsed_length = parse_statement_list(tokens.from(4), ast, function_symbols);

        ast.end_children(function_identifier_node);

        return 4 + parsed_length;
    }
    else if (tokens[2].type == '=') {
        // this is a variable def and assignment - these are done separately (for now)
        ast.push(ASTIdentifierNode(ASTNodeType::VariableDef, new_symbol_id));

        ASTNode* assignment_node = ast.push(ASTIdentifierNode(ASTNodeType::Assignment, new_symbol_id));

        uint32_t parsed_length = 1 + parse_expression(tokens.from(3), ast, symbols, 1, &assignment_node->child);

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
                uint32_t parsed_length = parse_def(token_slice.from(token_idx), ast, global_symbols);

                token_idx += parsed_length;
            } break;
            default:
                fail_at_token("Invalid top-level statement", token_slice[token_idx]);
        }
    }

    print_ast_node(ast.start, global_symbols, 0);
}
