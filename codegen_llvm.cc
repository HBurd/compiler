#include "codegen_llvm.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>

#include <cassert>
#include <vector>

using namespace llvm;

// for interfacing with llvm
static StringRef make_twine(SubString substr)
{
    return StringRef(substr.start, substr.len);
}

struct CodeEmitter
{
    LLVMContext llvm_ctxt;
    IRBuilder<> ir_builder;
    Module* module;

    CodeEmitter()
    :ir_builder(llvm_ctxt)
    {
        module = new Module("top", llvm_ctxt);
    }

    ~CodeEmitter()
    {
        delete module;
    }

    Value* emit_binop(ASTNode* subexpr, SubString value_name)
    {
        ASTNode* operand = subexpr->child;
        Value* lhs = emit_subexpr(operand, SubString());

        operand = operand->sibling;
        Value* rhs = emit_subexpr(operand, SubString());

        switch (subexpr->op)
        {
            case '+':
                return ir_builder.CreateAdd(lhs, rhs, make_twine(value_name));
            case '-':
                return ir_builder.CreateSub(lhs, rhs, make_twine(value_name));
            case '*':
                return ir_builder.CreateMul(lhs, rhs, make_twine(value_name));
            default:
                assert(false && "Unsupported operator");
        }
        return nullptr;
    }

    Value* emit_subexpr(ASTNode* subexpr, SubString value_name)
    {
        switch (subexpr->type)
        {
            case ASTNodeType::Identifier:
                return ConstantInt::get(IntegerType::get(llvm_ctxt, 32), 420);
            case ASTNodeType::Number:
                return ConstantInt::get(IntegerType::get(llvm_ctxt, 32), subexpr->value);
            case ASTNodeType::BinaryOperator:
                return emit_binop(subexpr, value_name);
            default:
                assert(false && "Invalid syntax tree - expected a subexpression");
        }
        return nullptr;
    }

    void emit_statement(ASTNode* statement, Array<SymbolData, MAX_SYMBOLS>* symbols)
    {
        switch (statement->type)
        {
            case ASTNodeType::VariableDef:
            case ASTNodeType::Assignment:
            {
                SubString name = symbols->data[statement->symbol_id].name;
                emit_subexpr(statement->child, name);
            } break;
            case ASTNodeType::FunctionDef:
                // do nothing
                break;
            case ASTNodeType::Return:
            {
                Value* ret_value = emit_subexpr(statement->child, SubString());
                ir_builder.CreateRet(ret_value);
            } break;
            default:
                assert(false && "Invalid syntax tree - expected a statement");
        }
    }

    void generate_function_def(ASTNode* function_def_node, Array<SymbolData, MAX_SYMBOLS>* symbols)
    {
        assert(function_def_node->child && function_def_node->child->type == ASTNodeType::ParameterList);

        std::vector<Type*> arg_types;
        FunctionType* function_type = FunctionType::get(Type::getVoidTy(llvm_ctxt), arg_types, false);
        Function* function = Function::Create(
            function_type,
            Function::ExternalLinkage,
            make_twine(symbols->data[function_def_node->symbol_id].name),
            module
        );

        ASTNode* statement_list = function_def_node->child->sibling;
        assert(statement_list && statement_list->type == ASTNodeType::StatementList);

        BasicBlock* entry = BasicBlock::Create(llvm_ctxt, "entry", function);
        ir_builder.SetInsertPoint(entry);

        ASTNode* statement = statement_list->child;

        while(statement)
        {
            emit_statement(statement, statement_list->symbols);
            statement = statement->sibling;
        }
    }
};

void output_ast(Array<ASTNode, MAX_AST_SIZE>* ast, Array<SymbolData, MAX_SYMBOLS>* symbols)
{
    CodeEmitter emitter;

    ASTNode* node = &ast->data[0];
    while (node)
    {
        switch (node->type)
        {
            case ASTNodeType::FunctionDef:
                emitter.generate_function_def(node, symbols);
                break;
            default:
                assert(false);  // unsupported
        }

        node = node->sibling;
    }
    
    emitter.module->print(errs(), nullptr);
}
