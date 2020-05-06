#pragma once

#include "lexer.h"
#include "util.h"

namespace ASTNodeType
{
    enum
    {
        Invalid,
        FunctionDef,
        ParameterList,
        StatementList,
        VariableDef,
        FunctionParameter,
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

struct ASTNode
{
    uint32_t type = ASTNodeType::Invalid;
    ASTNode* child = nullptr;
    ASTNode* sibling = nullptr; // next child

    ASTNode() = default;
    ASTNode(uint32_t type_): type(type_) {}
};

struct ASTBinOpNode : public ASTNode
{
    char op;

    ASTBinOpNode(char op_)
        :ASTNode(ASTNodeType::BinaryOperator),
        op(op_)
    {}
};

struct ASTNumberNode: public ASTNode
{
    uint64_t value;

    ASTNumberNode(uint64_t value_)
        :ASTNode(ASTNodeType::Number),
        value(value_)
    {}
};

struct ASTIdentifierNode: public ASTNode
{
    uint32_t symbol_id;

    ASTIdentifierNode(uint32_t type, uint32_t symbol_id_)
        :ASTNode(type),
        symbol_id(symbol_id_)
    {}
};

struct ASTStatementListNode: public ASTNode
{
    Array<SymbolData> symbols;

    ASTStatementListNode(Array<SymbolData> symbols_)
        :ASTNode(ASTNodeType::StatementList),
        symbols(symbols_)
    {}
};

struct AST
{
    static const uint32_t MAX_SIZE = 65536;

    uint32_t next = 0;
    ASTNode* start = nullptr;
    ASTNode** next_node_ref = &start;
    uint8_t data[MAX_SIZE];

    ASTNode* push_orphan(const ASTNode& node);
    ASTNode* push(const ASTNode& node);

    void begin_children(ASTNode* node);
    void end_children(ASTNode* node);
};

void parse(const std::vector<Token>& tokens, AST& ast, Array<SymbolData>& symbols);

