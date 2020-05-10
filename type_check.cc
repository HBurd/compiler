#include "type_check.h"

static void typecheck_statement_list(ASTNode* statement);

static uint32_t deduce_binop_type(uint32_t op, uint32_t lhs_type, uint32_t rhs_type)
{
    assert(lhs_type == rhs_type);
    
    switch (op)
    {
        case '<':
        case '>':
            return TypeId::Bool;
        default:
            return lhs_type;
    }
}

static uint32_t deduce_type(ASTNode* expr, Array<SymbolData> symbols)
{
    switch (expr->type)
    {
        case ASTNodeType::Number:
            return TypeId::U32;
        case ASTNodeType::Identifier:
            return symbols[static_cast<ASTIdentifierNode*>(expr)->symbol_id].type_id;
        case ASTNodeType::BinaryOperator: {
            uint32_t lhs_type = deduce_type(expr->child, symbols);
            uint32_t rhs_type = deduce_type(expr->child->sibling, symbols);
            return deduce_binop_type(static_cast<ASTBinOpNode*>(expr)->op, lhs_type, rhs_type);
        }
        default:
            return TypeId::Invalid;
    }
}

static void typecheck_statement(ASTNode* statement, Array<SymbolData> symbols)
{
    // TODO: need proper error messages here - can't do that without tokens currently
    switch (statement->type)
    {
        case ASTNodeType::VariableDef:
        case ASTNodeType::Assignment:
            assert(symbols[static_cast<ASTIdentifierNode*>(statement)->symbol_id].type_id == deduce_type(statement->child, symbols));
            break;
        case ASTNodeType::Return:
            if (statement->child)
            {
                deduce_type(statement->child, symbols);
            }
            break;
        case ASTNodeType::If:
        case ASTNodeType::While:
            assert(deduce_type(statement->child, symbols) == TypeId::Bool);
            typecheck_statement_list(statement->child->sibling);
            break;
        default:
            assert(false && "Unsupported");
    }
}

static void typecheck_statement_list(ASTNode* statement_list)
{
    assert(statement_list->type == ASTNodeType::StatementList);
    ASTNode* statement = statement_list->child;
    while (statement)
    {
        typecheck_statement(statement, static_cast<ASTStatementListNode*>(statement_list)->symbols);
        statement = statement->sibling;
    }
}

void check_types(const AST& ast)
{
    assert(ast.start->type == ASTNodeType::FunctionDef);

    ASTNode* statement_list = ast.start->child->sibling;

    typecheck_statement_list(statement_list);
}
