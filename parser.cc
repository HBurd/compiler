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
    [ASTNodeType::If] = "If",
    [ASTNodeType::While] = "While",
    [ASTNodeType::FunctionCall] = "FunctionCall",
};

uint8_t OPERATOR_PRECEDENCE[TokenType::Count] = {
    ['('] = 50,  // Function call
    ['*'] = 20,
    ['+'] = 10,
    ['-'] = 10,  // TODO: how to differentiate unary and binary
    ['<'] = 5,
    ['>'] = 5,
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

void AST::attach(ASTNode* node)
{
    *next_node_ref = node;
    next_node_ref = &node->sibling;
}

ASTNode* AST::push(const ASTNode& node)
{
    ASTNode* result = push_orphan(node);
    attach(result);

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

SymbolData* Scope::lookup_symbol(SubString name)
{
    for (uint32_t i = 0; i < symbols.length; ++i)
    {
        if (symbols[i].name == name)
        {
            return &symbols[i];
        }
    }

    // Symbol wasn't found in the current scope, so search the parent scope
    if (parent)
    {
        return parent->lookup_symbol(name);
    }
    return nullptr;
}

SymbolData* Scope::push(SubString name, uint32_t type_id)
{
    SymbolData new_symbol;
    new_symbol.name = name;
    new_symbol.type_id = type_id;
    symbols.push(new_symbol);

    return symbols.back();
}

// ----------------------------
// Functions for generating AST
// ----------------------------

static void parse_def(TokenReader& tokens, AST& ast, Scope& scope);
static void parse_statement_list(TokenReader& tokens, AST& ast, Scope& scope);

// Creates a subexpression consisting of operators of precedence >= precedence,
// starting at the initial position of the token reader.
// Returns the subexpression once the token reader has advanced onto a lower
// precedence operator.
static ASTNode* parse_expression(TokenReader& tokens, AST& ast, Scope& scope, uint32_t precedence)
{
    // Stick a subexpression on the AST, then advance onto an operator or terminator.
    ASTNode* result;
    if (tokens.peek().type == '(')
    {
        tokens.advance();

        result = parse_expression(tokens, ast, scope, 1);

        assert_at_token(tokens.peek().type == ')', "Missing ')'", tokens.peek());

        tokens.advance();      // move past ')'
    }
    else if (tokens.peek().type == TokenType::Name)
    {
        SymbolData* symbol = scope.lookup_symbol(tokens.peek().name);
        assert_at_token(symbol, "Unknown identifier", tokens.peek());

        result = ast.push_orphan(ASTIdentifierNode(ASTNodeType::Identifier, symbol));

        tokens.advance();
    }
    else if (tokens.peek().type == TokenType::Number)
    {
        result = ast.push_orphan(ASTNumberNode(tokens.peek().number_value));

        tokens.advance();
    }
    else
    {
        fail_at_token("Expected a subexpression", tokens.peek());
    }

    // Now we are sitting on an operator or terminator.

    uint32_t op_type = tokens.peek().type;
    uint32_t op_precedence = OPERATOR_PRECEDENCE[op_type];
    while (op_precedence >= precedence)
    {
        // Result is the lhs of the current operator.
        // Now build rhs.

        tokens.advance();

        if (op_type == '(')
        {
            // This is a function call

            ASTNode* arg = result;
            while (tokens.peek().type != ')')
            {
                arg->sibling = parse_expression(tokens, ast, scope, 1);
                assert_at_token(tokens.peek().type == ',' || tokens.peek().type == ')', "Expected ',' or ')'", tokens.peek());

                arg = arg->sibling;
            }

            // Advance past ')'
            tokens.advance();

            ASTNode* function_call_node = ast.push_orphan(ASTNode(ASTNodeType::FunctionCall));
            function_call_node->child = result;
            result = function_call_node;
        }
        else
        {
            // Construct rhs expression
            result->sibling = parse_expression(tokens, ast, scope, op_precedence + 1);

            ASTNode* op_node = ast.push_orphan(ASTBinOpNode(op_type));
            op_node->child = result;

            result = op_node;
        }

        // We should now be sitting after an op_precedence + 1 subexpression
        assert(OPERATOR_PRECEDENCE[tokens.peek().type] <= op_precedence);

        op_type = tokens.peek().type;
        op_precedence = OPERATOR_PRECEDENCE[op_type];
    }

    // Subexpression is terminated, either with a lower precedence operator
    // or a terminator character. If subexpression has ended on a terminator,
    // the caller must make sure that the terminator is valid.

    return result;
}

static void parse_if_or_while(TokenReader& tokens, AST& ast, Scope& scope)
{
    // we need to differentiate if and while to determine if an else can follow
    bool is_if = false;

    uint32_t statement_node_type;
    if (tokens.peek().type == TokenType::If)
    {
        statement_node_type = ASTNodeType::If;
        is_if = true;
    }
    else if (tokens.peek().type == TokenType::While)
    {
        statement_node_type = ASTNodeType::While;
    }
    else
    {
        assert(false);
    }

    ASTNode* statement_node = ast.push(statement_node_type);
    ast.begin_children(statement_node);

    tokens.advance();

    ASTNode* condition_node = parse_expression(tokens, ast, scope, 1);
    ast.attach(condition_node);

    assert_at_token(tokens.peek().type == '{', "Expected block following if", tokens.peek());
    parse_statement_list(tokens, ast, scope);

    if (is_if)
    {
        // check for else
        if (tokens.peek().type == TokenType::Else)
        {
            tokens.advance();
            parse_statement_list(tokens, ast, scope);
        }
    }

    ast.end_children(statement_node);
}

static void parse_statement(TokenReader& tokens, AST& ast, Scope& scope)
{
    if (tokens.peek().type == TokenType::Name && tokens.peek(1).type == ':')
    {
        // Parse definition
        parse_def(tokens, ast, scope);
    }
    else if (tokens.peek().type == TokenType::Name && tokens.peek(1).type == '=')
    {
        // Parse assignment
        SymbolData* symbol = scope.lookup_symbol(tokens.peek().name);
        assert_at_token(symbol, "Unknown symbol", tokens.peek());

        ASTNode* assign_node = ast.push(ASTIdentifierNode(ASTNodeType::Assignment, symbol));

        tokens.advance(2);

        assign_node->child = parse_expression(tokens, ast, scope, 1);
        assert_at_token(tokens.peek().type == ';', "Expected ';'", tokens.peek());

        tokens.advance();  // advance past semicolon
    }
    else if (tokens.peek().type == TokenType::Return)
    {
        ASTNode* return_node = ast.push(ASTNode(ASTNodeType::Return));
        tokens.advance();

        if (tokens.peek().type == ';')
        {
            tokens.advance();  // advance past semicolon
        }
        else
        {
            // Returning an expression
            return_node->child = parse_expression(tokens, ast, scope, 1);
            assert_at_token(tokens.peek().type == ';', "Expected ';'", tokens.peek());

            tokens.advance();  // advance past semicolon
        }
    }
    else if (tokens.peek().type == TokenType::If || tokens.peek().type == TokenType::While)
    {
        parse_if_or_while(tokens, ast, scope);
    }
    else
    {
        // Assume this is an expression (e.g. function call)
        ast.attach(parse_expression(tokens, ast, scope, 1));
        assert_at_token(tokens.peek().type == ';', "Expected ';'", tokens.peek());
        tokens.advance();  // advance past semicolon
    }
}

static void parse_parameter_list(TokenReader& tokens, AST& ast, Scope& scope, SymbolData* function_symbol)
{
    assert_at_token(tokens.peek().type == '(', "Expected '('", tokens.peek());

    ASTNode* parameter_list_node = ast.push(ASTNode(ASTNodeType::ParameterList));

    ast.begin_children(parameter_list_node);

    uint32_t param_count = 0;

    if (tokens.peek(1).type != ')')
    {
        do
        {
            tokens.advance();

            assert_at_token(tokens.peek().type == TokenType::Name, "Expected an identifier", tokens.peek());
            assert_at_token(tokens.peek(1).type == ':', "Expected ':'", tokens.peek(1));
            assert_at_token(tokens.peek(2).type == TokenType::TypeName, "Expected a type", tokens.peek(2));

            SymbolData* new_symbol = scope.push(tokens.peek().name, tokens.peek(2).type_id);

            ast.push(ASTIdentifierNode(ASTNodeType::FunctionParameter, new_symbol));

            ++param_count;

            tokens.advance(3);
        } while(tokens.peek().type == ',');
    }
    else
    {
        tokens.advance();
    }

    assert_at_token(tokens.peek().type == ')', "Expected ')'", tokens.peek());
    tokens.advance();

    ast.end_children(parameter_list_node);

    // Add parameter types to function info
    function_symbol->function_info = (FunctionInfo*) new char[sizeof(FunctionInfo) + 4 * param_count];
    function_symbol->function_info->param_count = param_count;

    ASTNode* param = (ASTIdentifierNode*)parameter_list_node->child;
    for (size_t i = 0; i < param_count; ++i, param = param->sibling)
    {
        assert(param);
        assert(param->type == ASTNodeType::FunctionParameter);
        function_symbol->function_info->param_types[i] = static_cast<ASTIdentifierNode*>(param)->symbol->type_id;
    }
    assert(!param);
}

static void parse_statement_list(TokenReader& tokens, AST& ast, Scope& scope)
{
    assert_at_token(tokens.peek().type == '{', "Expected '{'", tokens.peek());
    tokens.advance();

    ASTNode* statement_list_node = ast.push(ASTStatementListNode(scope));

    ast.begin_children(statement_list_node);

    while (!tokens.eof() && tokens.peek().type != '}')
    {
        parse_statement(tokens, ast, static_cast<ASTStatementListNode*>(statement_list_node)->scope);
    }

    ast.end_children(statement_list_node);

    assert_at_token(!tokens.eof(), "Expected '}'", tokens.peek(-1));
    tokens.advance();
}

static void parse_def(TokenReader& tokens, AST& ast, Scope& scope)
{

    assert_at_token(
        tokens.peek().type == TokenType::Name && tokens.peek(1).type == ':',
        "Invalid definition",
        tokens.peek());

    // check if an entry is already in the symbol table
    assert_at_token(!scope.lookup_symbol(tokens.peek().name), "Symbol already declared", tokens.peek());

    // figure out which type of def:
    if (tokens.peek(2).type == '(')
    {
        // this is a function def

        SymbolData* new_symbol = scope.push(tokens.peek().name, TypeId::Invalid);

        ASTNode* function_identifier_node = ast.push(ASTIdentifierNode(ASTNodeType::FunctionDef, new_symbol));

        ast.begin_children(function_identifier_node);

        // Need to make the function's symbol table here so we can add the parameters to the scope
        Scope function_scope;
        function_scope.parent = &scope;
        function_scope.symbols.max_length = MAX_SYMBOLS;
        function_scope.symbols.data = new SymbolData[MAX_SYMBOLS];

        tokens.advance(2);

        parse_parameter_list(tokens, ast, function_scope, new_symbol);

        if (tokens.peek().type == '-' && tokens.peek(1).type == '>')
        {
            assert_at_token(
                tokens.peek(2).type == TokenType::TypeName,
                "Expected type name",
                tokens.peek(2));

            new_symbol->function_info->return_type = tokens.peek(2).type_id;

            tokens.advance(3);
        }
        else
        {
            new_symbol->function_info->return_type = TypeId::None;
        }

        if (tokens.peek().type == ';')
        {
            // This is just a declaration, skip past semicolon
            tokens.advance();
        }
        else
        {
            parse_statement_list(tokens, ast, function_scope);
        }

        ast.end_children(function_identifier_node);
    }
    else if (tokens.peek(2).type == '=')
    {
        assert(false && "type inference not yet supported");
    }
    else if (tokens.peek(2).type == TokenType::TypeName
             && tokens.peek(3).type == '=') // This is a variable def
    {
        // the symbol has to be set later because it's not in the scope yet
        ASTNode* variable_def_node = ast.push(ASTIdentifierNode(ASTNodeType::VariableDef, nullptr));

        SubString variable_name = tokens.peek().name;
        uint32_t variable_type = tokens.peek(2).type_id;

        tokens.advance(4);

        variable_def_node->child = parse_expression(tokens, ast, scope, 1);

        // symbol is set here
        static_cast<ASTIdentifierNode*>(variable_def_node)->symbol = scope.push(variable_name, variable_type);

        tokens.advance();      // advance past semicolon
    }
    else
    {
        fail_at_token("Invalid definition", tokens.peek());
    }
}

static void print_ast_node(ASTNode* node, Scope& scope, uint32_t depth)
{

    for (uint32_t i = 0; i < depth; ++i)
    {
        std::cout << "  ";
    }

    if (node->type == ASTNodeType::BinaryOperator)
    {
        std::cout << (char)static_cast<ASTBinOpNode*>(node)->op << std::endl;
    }
    else if (node->type == ASTNodeType::Number)
    {
        std::cout << static_cast<ASTNumberNode*>(node)->value << std::endl;
    }
    else if (node->type == ASTNodeType::Identifier)
    {
        static_cast<ASTIdentifierNode*>(node)->symbol->name.print();
        std::cout << std::endl;
    }
    else if (node->type == ASTNodeType::Assignment)
    {
        std::cout << "Assignment(";
        static_cast<ASTIdentifierNode*>(node)->symbol->name.print();
        std::cout << ")" << std::endl;
    }
    else if (node->type == ASTNodeType::VariableDef)
    {
        std::cout << "VariableDef(";
        static_cast<ASTIdentifierNode*>(node)->symbol->name.print();
        std::cout << ")" << std::endl;
    }
    else
    {
        std::cout << AST_NODE_TYPE_NAME[node->type] << std::endl;
    }

    if (node->type == ASTNodeType::StatementList)
    {
        scope = static_cast<ASTStatementListNode*>(node)->scope;
    }

    ASTNode* child = node->child;
    while (child)
    {
        print_ast_node(child, scope, depth + 1);
        child = child->sibling;
    }
}

void parse(const std::vector<Token>& tokens, AST& ast, Scope& global_scope)
{
    TokenReader token_reader;
    token_reader.data = tokens.data();
    token_reader.length = tokens.size();

    while (!token_reader.eof())
    {
        // top level expressions
        switch (token_reader.peek().type)
        {
            case TokenType::Name: {
                parse_def(token_reader, ast, global_scope);

            } break;
            default:
                fail_at_token("Invalid top-level statement", token_reader.peek());
        }
    }

    //print_ast_node(ast.start, global_scope, 0);
    //print_ast_node(ast.start->sibling, global_scope, 0);
}
