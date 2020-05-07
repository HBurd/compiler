#include "type_check.h"

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
            assert(lhs_type == deduce_type(expr->child->sibling, symbols));
            return lhs_type;
        }
        default:
            return TypeId::Invalid;
    }
}

static void typecheck_statement(ASTNode* statement, Array<SymbolData> symbols)
{
    switch (statement->type)
    {
        case ASTNodeType::VariableDef:
        case ASTNodeType::Assignment:
            // TODO: need proper error messages here - can't do that without tokens currently
            assert(symbols[static_cast<ASTIdentifierNode*>(statement)->symbol_id].type_id == deduce_type(statement->child, symbols));
            break;
        case ASTNodeType::Return:
            if (statement->child)
            {
                deduce_type(statement->child, symbols);
            }
            break;
        default:
            assert(false && "Unsupported");
    }
}

void check_types(const AST& ast)
{
    assert(ast.start->type == ASTNodeType::FunctionDef);

    ASTNode* statement_list = ast.start->child->sibling;
    assert(statement_list->type == ASTNodeType::StatementList);

    ASTNode* statement = statement_list->child;
    while (statement)
    {
        typecheck_statement(statement, static_cast<ASTStatementListNode*>(statement_list)->symbols);
        statement = statement->sibling;
    }
}
