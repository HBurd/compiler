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
    [ASTNodeType::FunctionParameter] = "FunctionParameter",
};

uint8_t OPERATOR_PRECEDENCE[] = {
    ['*'] = 2,
    ['+'] = 1,
    ['-'] = 1,  // TODO: how to differentiate unary and binary
};

struct TokenReader {
    Token const * data = nullptr;
    uint32_t length = 0;
    uint32_t position = 0;

    Token peek(int32_t index = 0)
    {
        if ((int32_t)position + index >= (int32_t)length)
        {
            return {};
        }

        return data[position + index];
    }

    void advance(uint32_t amount = 1)
    {
        assert(position + amount <= length);
        position += amount;
    }

    bool eof()
    {
        return position >= length;
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
        case ASTNodeType::FunctionParameter:
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

static void parse_def(TokenReader& tokens, AST& ast, Array<SymbolData>& symbols);

// We stick expressions on the AST in RPN to make left to right precedence easier
static void parse_expression(TokenReader& tokens, AST& ast, Array<SymbolData>& symbols, uint8_t precedence, ASTNode** expr) {

    if (tokens.peek().type == '(') {
        tokens.advance();

        ASTNode* subexpr = nullptr;
        parse_expression(tokens, ast, symbols, 1, &subexpr);

        assert_at_token(tokens.peek().type == ')', "Missing ')'", tokens.peek());

        assert_at_token(subexpr, "Empty subexpression", tokens.peek());
        *expr = subexpr;

        tokens.advance();      // move past ')'
    }
    else if (tokens.peek().type == TokenType::Name || tokens.peek().type == TokenType::Number) {
        // recurse until precedence matches that of the next operator
        if (OPERATOR_PRECEDENCE[tokens.peek(1).type] >= precedence) {
            // this sticks the subexpr of higher precedence onto the AST
            parse_expression(tokens, ast, symbols, precedence + 1, expr);

            // now token_idx is after a precedence + 1 subexpression

            assert(OPERATOR_PRECEDENCE[tokens.peek().type] <= precedence);

            while (OPERATOR_PRECEDENCE[tokens.peek().type] == precedence) {
                uint32_t op = tokens.peek().type;
                tokens.advance();

                // now recurse so that the right expression is on the AST
                ASTNode* right_subexpr;
                parse_expression(tokens, ast, symbols, precedence + 1, &right_subexpr);

                (*expr)->sibling = right_subexpr;

                ASTNode* op_node = ast.push_orphan(ASTBinOpNode(op));
                op_node->child = *expr;
                *expr = op_node;
            }
        }
        else {
            switch (tokens.peek().type)
            {
                case TokenType::Name:
                    uint32_t symbol_id;
                    assert_at_token(lookup_symbol(symbols, tokens.peek().name, &symbol_id), "Unknown identifier", tokens.peek());

                    *expr = ast.push_orphan(ASTIdentifierNode(ASTNodeType::Identifier, symbol_id));
                    break;
                case TokenType::Number:
                    *expr = ast.push_orphan(ASTNumberNode(tokens.peek().number_value));
                    break;
                default:
                    assert(false);
            }
            tokens.advance();
        }
    }
    else {
        fail_at_token("Invalid expression", tokens.peek());
    }

    assert_at_token(
        OPERATOR_PRECEDENCE[tokens.peek().type] || tokens.peek().type == ';' || tokens.peek().type == ')',
        "Unknown token terminating expression",
        tokens.peek());

    assert(OPERATOR_PRECEDENCE[tokens.peek().type] < precedence);
}

static void parse_statement(TokenReader& tokens, AST& ast, Array<SymbolData>& symbols) {
    if (tokens.peek().type == TokenType::Name) {
        if (tokens.peek(1).type == ':') {
            // Parse declaration
            parse_def(tokens, ast, symbols);
        }
        else if (tokens.peek(1).type == '=') {
            // Parse assignment
            uint32_t symbol_id;
            bool found_symbol = lookup_symbol(symbols, tokens.peek().name, &symbol_id);
            assert_at_token(found_symbol, "Unknown symbol", tokens.peek());

            ASTNode* assign_node = ast.push(ASTIdentifierNode(ASTNodeType::Assignment, symbol_id));

            tokens.advance(2);

            parse_expression(tokens, ast, symbols, 1, &assign_node->child);
            assert_at_token(tokens.peek().type == ';', "Expected ';'", tokens.peek());

            tokens.advance();  // advance past semicolon
        }
    }
    else if (tokens.peek().type == TokenType::Return) {
        ASTNode* return_node = ast.push(ASTNode(ASTNodeType::Return)); // TODO: can only return an expression right now
        tokens.advance();
        parse_expression(tokens, ast, symbols, 1, &return_node->child);
        assert_at_token(tokens.peek().type == ';', "Expected ';'", tokens.peek());

        tokens.advance();  // advance past semicolon
    }
    else {
        fail_at_token("Invalid statement", tokens.peek());
    }
}

static void parse_parameter_list(TokenReader& tokens, AST& ast, Array<SymbolData>& symbols) {
    assert_at_token(tokens.peek().type == '(', "Expected '('", tokens.peek());

    tokens.advance();

    ASTNode* parameter_list_node = ast.push(ASTNode(ASTNodeType::ParameterList));

    ast.begin_children(parameter_list_node);

    if (tokens.peek().type != ')')
    {
        assert_at_token(tokens.peek().type == TokenType::Name, "Expected an identifier", tokens.peek());
        ast.push(ASTIdentifierNode(ASTNodeType::FunctionParameter, symbols.length));
        SymbolData new_symbol;
        new_symbol.name = tokens.peek().name;
        symbols.push(new_symbol);

        tokens.advance();

        while(tokens.peek().type == ',')
        {
            tokens.advance();

            assert_at_token(tokens.peek().type == TokenType::Name, "Expected an identifier", tokens.peek());
            ast.push(ASTIdentifierNode(ASTNodeType::FunctionParameter, symbols.length));
            new_symbol.name = tokens.peek().name;
            symbols.push(new_symbol);

            tokens.advance();
        }
    }

    assert_at_token(tokens.peek().type == ')', "Expected ')'", tokens.peek());
    tokens.advance();

    ast.end_children(parameter_list_node);
}

static void parse_statement_list(TokenReader& tokens, AST& ast, Array<SymbolData>& symbols) {
    assert_at_token(tokens.peek().type == '{', "Expected '{'", tokens.peek());
    tokens.advance();

    ASTNode* statement_list_node = ast.push(ASTStatementListNode(symbols));

    ast.begin_children(statement_list_node);

    while (!tokens.eof() && tokens.peek().type != '}') {
        parse_statement(tokens, ast, static_cast<ASTStatementListNode*>(statement_list_node)->symbols);
    }

    ast.end_children(statement_list_node);

    assert_at_token(!tokens.eof(), "Expected '}'", tokens.peek(-1));
    tokens.advance();
}

static void parse_def(TokenReader& tokens, AST& ast, Array<SymbolData>& symbols) {

    assert_at_token(
        tokens.peek().type == TokenType::Name && tokens.peek(1).type == ':',
        "Invalid definition",
        tokens.peek());

    // check if an entry is already in the symbol table
    assert_at_token(!lookup_symbol(symbols, tokens.peek().name, nullptr), "Symbol already declared", tokens.peek());

    uint32_t new_symbol_id = symbols.length;

    SymbolData new_symbol;
    new_symbol.name = tokens.peek().name;
    symbols.push(new_symbol);

    // figure out which type of def:
    if (tokens.peek(2).type == '(') {
        // this is a function def
        ASTNode* function_identifier_node = ast.push(ASTIdentifierNode(ASTNodeType::FunctionDef, new_symbol_id));

        ast.begin_children(function_identifier_node);

        // Need to make the function's symbol table here so we can add the parameters to the scope
        Array<SymbolData> function_symbols;
        function_symbols.max_length = MAX_SYMBOLS;
        function_symbols.data = new SymbolData[MAX_SYMBOLS];

        tokens.advance(2);

        parse_parameter_list(tokens, ast, function_symbols);
        parse_statement_list(tokens, ast, function_symbols);

        ast.end_children(function_identifier_node);
    }
    else if (tokens.peek(2).type == '=') {
        // this is a variable / constant def
        ASTNode* variable_def_node = ast.push(ASTIdentifierNode(ASTNodeType::VariableDef, new_symbol_id));

        tokens.advance(3);

        parse_expression(tokens, ast, symbols, 1, &variable_def_node->child);

        tokens.advance();      // advance past semicolon
    }
    else {
        fail_at_token("Invalid definition", tokens.peek());
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

    TokenReader token_reader;
    token_reader.data = tokens.data();
    token_reader.length = tokens.size();

    while (!token_reader.eof()) {
        // top level expressions
        switch (token_reader.peek().type) {
            case TokenType::Name: {
                parse_def(token_reader, ast, global_symbols);

            } break;
            default:
                fail_at_token("Invalid top-level statement", token_reader.peek());
        }
    }

    print_ast_node(ast.start, global_symbols, 0);
}
