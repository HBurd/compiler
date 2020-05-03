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
        Number,
        BinaryOperator,

        Count
    };
}

extern const char* AST_NODE_TYPE_NAME[ASTNodeType::Count];
extern uint8_t OPERATOR_PRECEDENCE[TokenType::Count];

constexpr uint32_t MAX_SYMBOLS = 1024;
struct SymbolData
{
    SubString name;
};

struct ASTNode { 
    uint32_t type = ASTNodeType::Invalid;
    ASTNode* child = nullptr;
    ASTNode* sibling = nullptr; // next child

    union {
        char op;        // for operator
        uint64_t value; // for number
        uint32_t symbol_id;
        Array<SymbolData, MAX_SYMBOLS>* symbols;
    };

    ASTNode() = default;
    ASTNode(uint32_t type_): type(type_){}
};

// no special thought went into picking these
constexpr uint32_t MAX_AST_SIZE = 65536;

// returns a new'd pointer
void parse(const std::vector<Token>& tokens, Array<ASTNode, MAX_AST_SIZE>* ast, Array<SymbolData, MAX_SYMBOLS>* symbols);
