#pragma once

#include "lexer.h"
#include "util.h"

namespace ASTNodeType {
    enum {
        Invalid,
        FunctionDef,
        ParameterList,
        StatementList,
        VariableDef,
        Assignment,
        Return,
        Identifier,
        BinaryOperator,

        Count
    };
}

extern const char* AST_NODE_TYPE_NAME[ASTNodeType::Count];
extern uint8_t OPERATOR_PRIORITY[TokenType::Count];

struct ASTNode { 
    uint32_t type = ASTNodeType::Invalid;
    uint32_t children = 0;
    ASTNode* next = nullptr; // next child

    union {
        char op;
    };

    ASTNode() = default;
    ASTNode(uint32_t type_): type(type_){}
};

constexpr uint32_t MAX_AST_SIZE = 65536;

// returns a new'd pointer
Array<ASTNode, MAX_AST_SIZE>* parse(const std::vector<Token>& tokens);
