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
        If,
        While,

        Count
    };
}

extern const char* AST_NODE_TYPE_NAME[ASTNodeType::Count];

namespace TypeId
{
    enum
    {
        Invalid,
        U8,
        I8,
        U16,
        I16,
        U32,
        I32,
        U64,
        I64,

        Bool,

        Count
    };
}

constexpr uint32_t MAX_SYMBOLS = 1024;
struct SymbolData
{
    SubString name;
    uint32_t type_id = TypeId::Invalid;

    void* codegen_data = nullptr;
};

struct Scope
{
    Scope* parent = nullptr;
    Array<SymbolData> symbols;

    SymbolData* push(SubString name, uint32_t type_id);
    SymbolData* lookup_symbol(SubString name);
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
    uint32_t op;

    //---------------------
    // Set in type checking
    //---------------------
    bool is_signed = false;  // for greater than and less than

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
    SymbolData* symbol;

    ASTIdentifierNode(uint32_t type, SymbolData* symbol_)
        :ASTNode(type),
        symbol(symbol_)
    {}
};

struct ASTStatementListNode: public ASTNode
{
    Scope scope;

    ASTStatementListNode(Scope scope_)
        :ASTNode(ASTNodeType::StatementList),
        scope(scope_)
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
    void attach(ASTNode* node);

    void begin_children(ASTNode* node);
    void end_children(ASTNode* node);
};

void parse(const std::vector<Token>& tokens, AST& ast, Scope& global_scope);

