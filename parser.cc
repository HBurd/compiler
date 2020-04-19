#include "parser.h"
#include "util.h"

#include <iostream>
#include <cassert>
#include <cstdlib>

// TODO: should be accessible anywhere
static void compile_assert(int condition, const char* err_msg, uint32_t line) {
    if (!condition) {
        std::cout << "Line " << line << ": " << err_msg << std::endl;
        exit(1);
    }
}

static void compile_fail(const char* err_msg, uint32_t line) {
    compile_assert(false, err_msg, line);
}

const char* AST_NODE_TYPE_NAME[] = {
    [ASTNodeType::Invalid] = "Invalid",
    [ASTNodeType::FunctionDef] = "FunctionDef",
    [ASTNodeType::ParameterList] = "ParameterList",
    [ASTNodeType::StatementList] = "StatementList",
    [ASTNodeType::VariableDef] = "VariableDef",
    [ASTNodeType::Assignment] = "Assignment",
    [ASTNodeType::Return] = "Return",
    [ASTNodeType::Identifier] = "Identifier",
    [ASTNodeType::BinaryOperator] = "BinaryOperator",
};

uint8_t OPERATOR_PRECEDENCE[] = {
    ['*'] = 2,
    ['+'] = 1,
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

// ----------------------------
// Functions for generating AST
// ----------------------------

static uint32_t parse_def(TokenSlice tokens, Array<ASTNode, MAX_AST_SIZE>* ast);

// We stick expressions on the AST in RPN to make left to right precedence easier
static uint32_t parse_expression(TokenSlice tokens, Array<ASTNode, MAX_AST_SIZE>* ast, uint8_t precedence) {

    uint32_t token_idx = 0;

    compile_assert(tokens[token_idx].type == TokenType::Identifier, "Invalid expression", tokens[token_idx].line);

    // recurse until precedence matches that of the next operator
    if (OPERATOR_PRECEDENCE[tokens[token_idx + 1].type] > precedence) {
        token_idx += parse_expression(tokens, ast, precedence + 1);
    }
    else {
        ast->push(ASTNode{ASTNodeType::Identifier});
    }
    
    while (OPERATOR_PRECEDENCE[tokens[token_idx + 1].type] == precedence) {
        uint32_t op_type = tokens[token_idx + 1].type;
        token_idx += 2;

        // now recurse so that the left expression is on the AST
        token_idx += parse_expression(tokens.from(token_idx), ast, precedence + 1);

        ASTNode op_node{ASTNodeType::BinaryOperator};
        std::cout << (char)op_type << std::endl;
        op_node.op = op_type;
        ast->push(op_node);
    }

    return token_idx;
}

static uint32_t parse_statement(TokenSlice tokens, Array<ASTNode, MAX_AST_SIZE>* ast) {
    if (tokens.length < 3) { // at least 2 tokens and a semicolon
        return 0;
    }

    if (tokens[0].type == TokenType::Identifier) {
        if (tokens[1].type == ':') {
            uint32_t parse_length = parse_def(tokens, ast);
            compile_assert(parse_length, "Invalid definition", tokens[0].line);
            return parse_length;
        }
        else if (tokens[1].type == '=') {
            ast->push(ASTNode{ASTNodeType::Assignment}).children = 1;
            uint32_t parse_length = 2 + parse_expression(tokens.from(2), ast, 1);
            std::cout << "assignment expr" << std::endl;
            compile_assert(parse_length, "Invalid expression on rhs of assignment", tokens[1].line);
            return 2 + parse_length;
        }
    }
    else if (tokens[0].type == TokenType::Return) {
        ast->push(ASTNode{ASTNodeType::Return}).children = 1; // TODO: can only return an expression right now
        uint32_t parse_length = 2 + parse_expression(tokens.from(1), ast, 1);
        std::cout << "return expr" << std::endl;
        compile_assert(parse_length, "Invalid expression after return", tokens[1].line);
        return 1 + parse_length;
    }

    std::cout << "Invalid statement starting with token id " << tokens[0].type << ", line " << tokens[0].line << std::endl;

    return 0;
}

static uint32_t parse_statement_list(TokenSlice tokens, Array<ASTNode, MAX_AST_SIZE>* ast) {
    if (tokens.length < 2) { // shortest is {}
        return 0;
    }

    compile_assert(tokens[0].type == '{', "Expected '{'", tokens[0].line);

    ASTNode* statement_list_node = &ast->push(ASTNode{ASTNodeType::StatementList});    

    uint32_t token_idx = 1;
    while (token_idx < tokens.length && tokens[token_idx].type != '}') {
        uint32_t parse_length = parse_statement(tokens.from(token_idx), ast);
        compile_assert(parse_length, "Invalid statement", tokens[token_idx].line);

        ++statement_list_node->children;
        token_idx += parse_length;
    }

    return token_idx + 1;
}

constexpr uint32_t MIN_DEF_LEN = 6; // identifier: () {}

static uint32_t parse_def(TokenSlice tokens, Array<ASTNode, MAX_AST_SIZE>* ast) {
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

        ast->push(ASTNode{ASTNodeType::FunctionDef}).children = 2;
        ast->push(ASTNode{ASTNodeType::ParameterList});

        uint32_t parsed_length = parse_statement_list(tokens.from(4), ast);
        compile_assert(parsed_length, "Invalid function body", tokens[3].line);

        return 4 + parsed_length;
    }
    else if (tokens[2].type == '=') {
        // this is a variable / constant def
        ast->push(ASTNode{ASTNodeType::VariableDef}).children = 1;

        uint32_t parsed_length = 2 + parse_expression(tokens.from(3), ast, 1);
        std::cout << "def expr, line " << tokens[3].line << std::endl;
        compile_assert(parsed_length, "Invalid expression on rhs of variable definition", tokens[2].line);

        return 3 + parsed_length;
    }
    else {
        return 0;
    }
}



// ----------------------------
// Functions for validating AST
// ----------------------------

static void validate_function_def(const Array<ASTNode, MAX_AST_SIZE>& ast, uint32_t* ast_pos);
static void validate_variable_def(const Array<ASTNode, MAX_AST_SIZE>& ast, uint32_t* ast_pos);

static void validate_expression(const Array<ASTNode, MAX_AST_SIZE>& ast, uint32_t* ast_pos) {
    // TODO:
    *ast_pos += 1;
}

static void validate_assignment(const Array<ASTNode, MAX_AST_SIZE>& ast, uint32_t* ast_pos) {
    assert(ast[*ast_pos].type == ASTNodeType::Assignment);
    assert(ast[*ast_pos].children == 1); // TODO: Don't hardcode

    *ast_pos += 1;
    validate_expression(ast, ast_pos);
}

static void validate_return(const Array<ASTNode, MAX_AST_SIZE>& ast, uint32_t* ast_pos) {
    assert(ast[*ast_pos].type == ASTNodeType::Return);
    assert(ast[*ast_pos].children == 1); // TODO: Don't hardcode

    *ast_pos += 1;
    validate_expression(ast, ast_pos);
}

static void validate_statement(const Array<ASTNode, MAX_AST_SIZE>& ast, uint32_t* ast_pos) {
    if (ast[*ast_pos].type == ASTNodeType::FunctionDef) {
        validate_function_def(ast, ast_pos);
    }
    else if (ast[*ast_pos].type == ASTNodeType::VariableDef) {
        validate_variable_def(ast, ast_pos);
    }
    else if (ast[*ast_pos].type == ASTNodeType::Assignment) {
        validate_assignment(ast, ast_pos);
    }
    else if (ast[*ast_pos].type == ASTNodeType::Return) {
        validate_return(ast, ast_pos);
    }
    else {
        // not a statement
        assert(false);
    }
}

static void validate_parameter_list(const Array<ASTNode, MAX_AST_SIZE>& ast, uint32_t* ast_pos) {
    assert(ast[*ast_pos].type == ASTNodeType::ParameterList);
    assert(ast[*ast_pos].children == 0); // don't support parameters yet

    *ast_pos += 1;
}

static void validate_statement_list(const Array<ASTNode, MAX_AST_SIZE>& ast, uint32_t* ast_pos) {
    assert(ast[*ast_pos].type == ASTNodeType::StatementList);

    uint32_t num_children = ast[*ast_pos].children;
    *ast_pos += 1;

    for (; num_children > 0; --num_children)
    {
        validate_statement(ast, ast_pos);
    }
}

static void validate_function_def(const Array<ASTNode, MAX_AST_SIZE>& ast, uint32_t* ast_pos) {

    assert(ast[*ast_pos].children == 2); // TODO: 2 shouldn't be hardcoded

    *ast_pos += 1;
    validate_parameter_list(ast, ast_pos);
    validate_statement_list(ast, ast_pos);
}

static void validate_variable_def(const Array<ASTNode, MAX_AST_SIZE>& ast, uint32_t* ast_pos) {
    assert(ast[*ast_pos].type == ASTNodeType::VariableDef);
    assert(ast[*ast_pos].children == 1); // TODO: Don't hardcode

    *ast_pos += 1;
    validate_expression(ast, ast_pos);
}

static void validate_def(const Array<ASTNode, MAX_AST_SIZE>& ast, uint32_t* ast_pos) {
    if (ast[*ast_pos].type == ASTNodeType::FunctionDef) {
        validate_function_def(ast, ast_pos);
    }
    else if (ast[*ast_pos].type != ASTNodeType::VariableDef) {
        validate_variable_def(ast, ast_pos);
    }
    else {
        // not a definition
        assert(false);
    }
}

static void validate_ast(const Array<ASTNode, MAX_AST_SIZE>& ast) {
    uint32_t ast_pos = 0;
    while(ast_pos < ast.size) {
        validate_def(ast, &ast_pos);
    }
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
            case TokenType::Identifier: {
                uint32_t parsed_length = parse_def(token_slice.from(token_idx), ast);
                compile_assert(parsed_length != 0, "Invalid definition", token_slice[token_idx].line);

                token_idx += parsed_length;
            } break;
            default:
                compile_fail("Invalid top-level statement", token_slice[token_idx].line);
        }
    }

    std::cout << ast->size << std::endl;

    // now output the AST for test
    for (uint32_t i = 0; i < ast->size; ++i) {
        std::cout << AST_NODE_TYPE_NAME[ast->data[i].type]
                  << "(" << ast->data[i].children << ")";

        if (ast->data[i].type == ASTNodeType::BinaryOperator) {
            std::cout << ast->data[i].op;
        }
        std::cout << std::endl;
    }

    // TODO: This is only here for testing
    //validate_ast(*ast);

    return ast;
}
