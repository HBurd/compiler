#include "codegen_llvm.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Verifier.h>

#include <cassert>
#include <vector>

// for interfacing with llvm
static llvm::StringRef make_twine(SubString substr)
{
    return llvm::StringRef(substr.start, substr.len);
}

static llvm::IntegerType* get_integer_type(uint32_t type_id, llvm::LLVMContext& llvm_ctxt)
{
    switch (type_id)
    {
        case TypeId::U8:
        case TypeId::I8:
            return llvm::Type::getInt8Ty(llvm_ctxt);
        case TypeId::U16:
        case TypeId::I16:
            return llvm::Type::getInt16Ty(llvm_ctxt);
        case TypeId::U32:
        case TypeId::I32:
            return llvm::Type::getInt32Ty(llvm_ctxt);
        case TypeId::U64:
        case TypeId::I64:
            return llvm::Type::getInt64Ty(llvm_ctxt);
        default:
            return nullptr;
    }
}

struct CodeEmitter
{
    llvm::LLVMContext llvm_ctxt;
    llvm::IRBuilder<> ir_builder;
    llvm::Module* module;

    CodeEmitter()
    :ir_builder(llvm_ctxt)
    {
        module = new llvm::Module("top", llvm_ctxt);
    }

    ~CodeEmitter()
    {
        delete module;
    }

    llvm::Value* emit_binop(ASTBinOpNode* subexpr, SymbolData* symbol)
    {
        ASTNode* operand = subexpr->child;
        llvm::Value* lhs = emit_subexpr(operand, nullptr);
        llvm::Value* rhs = emit_subexpr(operand->sibling, nullptr);

        SubString value_name;
        if (symbol)
        {
            value_name = symbol->name;
        }

        switch (subexpr->op)
        {
            case '+':
                return ir_builder.CreateAdd(lhs, rhs, make_twine(value_name));
            case '-':
                return ir_builder.CreateSub(lhs, rhs, make_twine(value_name));
            case '*':
                return ir_builder.CreateMul(lhs, rhs, make_twine(value_name));
            case '<':
                if (subexpr->is_signed)
                    return ir_builder.CreateICmpSLT(lhs, rhs, make_twine(value_name));
                else
                    return ir_builder.CreateICmpULT(lhs, rhs, make_twine(value_name));
            case '>':
                if (subexpr->is_signed)
                    return ir_builder.CreateICmpSGT(lhs, rhs, make_twine(value_name));
                else
                    return ir_builder.CreateICmpUGT(lhs, rhs, make_twine(value_name));
            default:
                assert(false && "Unsupported operator");
        }
        return nullptr;
    }

    llvm::Value* emit_subexpr(ASTNode* subexpr, SymbolData* symbol)
    {
        llvm::Value* result;
        switch (subexpr->type)
        {
            case ASTNodeType::Identifier:
                result = (llvm::Value*)static_cast<ASTIdentifierNode*>(subexpr)->symbol->codegen_data;
                break;
            case ASTNodeType::Number:
                // TODO: after type checking the number will actually have a type, so don't hardcode
                result = llvm::ConstantInt::get(get_integer_type(TypeId::U32, llvm_ctxt), static_cast<ASTNumberNode*>(subexpr)->value);
                break;
            case ASTNodeType::BinaryOperator:
                result = emit_binop(static_cast<ASTBinOpNode*>(subexpr), symbol);
                break;
            default:
                assert(false && "Invalid syntax tree - expected a subexpression");
        }

        // TODO: I think this doesn't belong here
        if (symbol)
        {
            symbol->codegen_data = result;
        }
        return result;
    }

    void emit_statement(ASTNode* statement)
    {
        switch (statement->type)
        {
            case ASTNodeType::VariableDef:
            case ASTNodeType::Assignment:
            {
                emit_subexpr(statement->child, static_cast<ASTIdentifierNode*>(statement)->symbol);
            } break;
            case ASTNodeType::FunctionDef:
                // do nothing
                break;
            case ASTNodeType::Return:
            {
                llvm::Value* ret_value = emit_subexpr(statement->child, nullptr);
                ir_builder.CreateRet(ret_value);
            } break;
            case ASTNodeType::If:
            {
                llvm::Function* function = ir_builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* then_block = llvm::BasicBlock::Create(llvm_ctxt, "then", function);
                llvm::BasicBlock* else_block = llvm::BasicBlock::Create(llvm_ctxt, "else", function);
                llvm::BasicBlock* fi_block = llvm::BasicBlock::Create(llvm_ctxt, "end_if", function);

                llvm::Value* condition_value = emit_subexpr(statement->child, nullptr);
                ir_builder.CreateCondBr(condition_value, then_block, else_block);

                ir_builder.SetInsertPoint(then_block);
                emit_statement_list(statement->child->sibling);
                ir_builder.CreateBr(fi_block);
                if (statement->child->sibling->sibling)
                {
                    // this is the statement list corresponding to the else block
                    ir_builder.SetInsertPoint(else_block);
                    emit_statement_list(statement->child->sibling->sibling);
                    ir_builder.CreateBr(fi_block);
                }
                ir_builder.SetInsertPoint(fi_block);
            } break;
            case ASTNodeType::While:
            {
                llvm::Function* function = ir_builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* do_block = llvm::BasicBlock::Create(llvm_ctxt, "do", function);
                llvm::BasicBlock* fi_block = llvm::BasicBlock::Create(llvm_ctxt, "end_do", function);

                llvm::Value* condition_value = emit_subexpr(statement->child, nullptr);
                ir_builder.CreateCondBr(condition_value, do_block, fi_block);

                ir_builder.SetInsertPoint(do_block);
                emit_statement_list(statement->child->sibling);
                llvm::Value* end_condition_value = emit_subexpr(statement->child, nullptr);
                ir_builder.CreateCondBr(end_condition_value, do_block, fi_block);
                ir_builder.SetInsertPoint(fi_block);
            } break;
            default:
                assert(false && "Invalid syntax tree - expected a statement");
        }
    }

    void emit_statement_list(ASTNode* statement_list)
    {
        assert(statement_list->type == ASTNodeType::StatementList);

        ASTNode* statement = statement_list->child;

        while(statement)
        {
            emit_statement(statement);
            statement = statement->sibling;
        }
    }

    void generate_function_def(ASTNode* function_def_node)
    {
        ASTNode* parameter_list = function_def_node->child;
        assert(parameter_list && parameter_list->type == ASTNodeType::ParameterList);

        ASTStatementListNode* statement_list = static_cast<ASTStatementListNode*>(function_def_node->child->sibling);
        assert(statement_list && statement_list->type == ASTNodeType::StatementList);

        std::vector<llvm::Type*> arg_types;
        {
            ASTNode* parameter = parameter_list->child;
            while (parameter)
            {
                arg_types.push_back(get_integer_type(static_cast<ASTIdentifierNode*>(parameter)->symbol->type_id, llvm_ctxt));
                parameter = parameter->sibling;
            }
        }

        llvm::FunctionType* function_type = llvm::FunctionType::get(get_integer_type(TypeId::U32, llvm_ctxt), arg_types, false);
        llvm::Function* function = llvm::Function::Create(
            function_type,
            llvm::Function::ExternalLinkage,
            make_twine(static_cast<ASTIdentifierNode*>(function_def_node)->symbol->name),
            module
        );

        // set function arg names and values
        {
            ASTIdentifierNode* parameter = static_cast<ASTIdentifierNode*>(function_def_node->child->child);
            auto arg = function->arg_begin();
            while (parameter)
            {
                assert(arg != function->arg_end());

                arg->setName(make_twine(parameter->symbol->name));
                parameter->symbol->codegen_data = &(*arg);  // convert iterator to pointer

                parameter = static_cast<ASTIdentifierNode*>(parameter->sibling);
                ++arg;
            }
        }

        llvm::BasicBlock* entry = llvm::BasicBlock::Create(llvm_ctxt, "entry", function);
        ir_builder.SetInsertPoint(entry);

        emit_statement_list(statement_list);

        llvm::verifyFunction(*function);
    }
};

void output_ast(AST& ast)
{
    CodeEmitter emitter;

    ASTNode* node = ast.start;
    while (node)
    {
        switch (node->type)
        {
            case ASTNodeType::FunctionDef:
                emitter.generate_function_def(node);
                break;
            default:
                assert(false);  // unsupported
        }

        node = node->sibling;
    }
    
    emitter.module->print(llvm::errs(), nullptr);
}
