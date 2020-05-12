#include "type_check.h"

static void set_statement_list_type_info(ASTNode* statement);

static bool is_signed_integer(uint32_t type_id)
{
    return type_id == TypeId::I8
        || type_id == TypeId::I16
        || type_id == TypeId::I32
        || type_id == TypeId::I64;
}

static uint32_t deduce_binop_result_type(uint32_t op, uint32_t lhs_type, uint32_t rhs_type)
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

static void set_binop_type_info(ASTBinOpNode* binop, uint32_t lhs_type, uint32_t rhs_type)
{
    // TODO: This assertion is temporary - I don't think this will be the case in general
    assert(lhs_type == rhs_type);

    if (binop->op == '<' || binop->op == '>')
    {
        binop->is_signed = is_signed_integer(lhs_type);
    }
}

static uint32_t set_expr_type_info(ASTNode* expr)
{
    switch (expr->type)
    {
        case ASTNodeType::Number:
            return TypeId::U32;
        case ASTNodeType::Identifier:
            return static_cast<ASTIdentifierNode*>(expr)->symbol->type_id;
        case ASTNodeType::BinaryOperator: {
            uint32_t lhs_type = set_expr_type_info(expr->child);
            uint32_t rhs_type = set_expr_type_info(expr->child->sibling);

            set_binop_type_info(static_cast<ASTBinOpNode*>(expr), lhs_type, rhs_type);

            return deduce_binop_result_type(static_cast<ASTBinOpNode*>(expr)->op, lhs_type, rhs_type);
        }
        default:
            return TypeId::Invalid;
    }
}

static void set_statement_type_info(ASTNode* statement)
{
    // TODO: need proper error messages here - can't do that without tokens currently
    switch (statement->type)
    {
        case ASTNodeType::VariableDef:
        case ASTNodeType::Assignment:
            assert(static_cast<ASTIdentifierNode*>(statement)->symbol->type_id == set_expr_type_info(statement->child));
            break;
        case ASTNodeType::Return:
            if (statement->child)
            {
                set_expr_type_info(statement->child);
            }
            break;
        case ASTNodeType::If:
        case ASTNodeType::While:
            assert(set_expr_type_info(statement->child) == TypeId::Bool);
            set_statement_list_type_info(statement->child->sibling);
            break;
        default:
            assert(false && "Unsupported");
    }
}

static void set_statement_list_type_info(ASTNode* statement_list)
{
    assert(statement_list->type == ASTNodeType::StatementList);
    ASTNode* statement = statement_list->child;
    while (statement)
    {
        set_statement_type_info(statement);
        statement = statement->sibling;
    }
}

void set_ast_type_info(AST& ast)
{
    assert(ast.start->type == ASTNodeType::FunctionDef);

    ASTNode* statement_list = ast.start->child->sibling;

    set_statement_list_type_info(statement_list);
}
