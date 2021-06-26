#pragma once

#include "lexer.h"
#include "util.h"
#include "codegen_llvm.h"
#include <cassert>

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
        FunctionCall,

        Count
    };
}

extern const char* AST_NODE_TYPE_NAME[ASTNodeType::Count];

namespace TypeId
{
    enum
    {
        Invalid,
        None,
        U8,
        I8,
        U16,
        I16,
        U32,
        I32,
        U64,
        I64,

        Bool,

        Function,

        Count
    };
}

struct FunctionInfo
{
    uint32_t return_type;
    uint32_t param_count;
    uint32_t param_types[];
};


constexpr uint32_t MAX_SYMBOLS = 1024;
struct SymbolData
{
    SubString name;
    uint32_t type_id = TypeId::Invalid;
    FunctionInfo* function_info = nullptr;

    SymbolData_Codegen codegen_data = nullptr;
};

struct Scope
{
    Scope* parent = nullptr;
    Array<SymbolData> symbols;

    SymbolData* push(SubString name, uint32_t type_id);
    SymbolData* lookup_symbol(SubString name);
};

template <typename T>
struct ASTIterator
{
    T* node;

    ASTIterator(T* node_) : node(node_) {}

    T* operator*()
    {
        return node;
    }

    const T* operator*() const
    {
        return node;
    }

    // Prefix increment
    ASTIterator& operator++()
    {
        node = static_cast<T*>(node->sibling);
        return *this;
    }

    // Postfix increment
    ASTIterator operator++(int) const
    {
        ASTIterator new_it = *this;
        new_it.node = node->sibling;
        return new_it;
    }

    bool operator==(const ASTIterator& rhs) const
    {
        return node == rhs.node;
    }

    bool operator!=(const ASTIterator& rhs) const
    {
        return node != rhs.node;
    }
};

struct ASTNode
{
    uint32_t type = ASTNodeType::Invalid;
    ASTNode* child = nullptr;
    ASTNode* sibling = nullptr; // next child

    ASTNode() = default;
    ASTNode(uint32_t type_): type(type_) {}

    ASTIterator<ASTNode> begin()
    {
        return child;
    }

    ASTIterator<ASTNode> end()
    {
        return nullptr;
    }
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

    ASTNode* lhs() { return child; }
    ASTNode* rhs() { return child->sibling; }
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

struct ASTParameterListNode: public ASTNode
{
    ASTIterator<ASTIdentifierNode> begin()
    {
        assert(!child || child->type == ASTNodeType::FunctionParameter);
        return static_cast<ASTIdentifierNode*>(child);
    }

    ASTIterator<ASTIdentifierNode> end()
    {
        return nullptr;
    }
};

struct ASTStatementListNode: public ASTNode
{
    Scope scope;

    ASTStatementListNode(Scope scope_)
        :ASTNode(ASTNodeType::StatementList),
        scope(scope_)
    {}
};

struct ASTFunctionDefNode: public ASTIdentifierNode
{
    ASTParameterListNode* parameters()
    {
        assert(child->type == ASTNodeType::ParameterList);
        return static_cast<ASTParameterListNode*>(child);
    };

    ASTStatementListNode* body()
    {
        assert(!child->sibling || child->sibling->type == ASTNodeType::StatementList);
        return static_cast<ASTStatementListNode*>(child->sibling);
    };
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

