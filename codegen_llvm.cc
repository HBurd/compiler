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

struct SymbolTable
{
    Array<SymbolData, MAX_SYMBOLS>* symbol_data;
    Value** values;
};

struct CodeEmitter
{
    LLVMContext llvm_ctxt;
    IRBuilder<> ir_builder;
    Module* module;

    static constexpr uint32_t ANONYMOUS_VALUE = 0xFFFFFFFF;

    CodeEmitter()
    :ir_builder(llvm_ctxt)
    {
        module = new Module("top", llvm_ctxt);
    }

    ~CodeEmitter()
    {
        delete module;
    }

    Value* emit_binop(ASTNode* subexpr, uint32_t symbol_id, SymbolTable symbols)
    {
        ASTNode* operand = subexpr->child;
        Value* lhs = emit_subexpr(operand, ANONYMOUS_VALUE, symbols);

        operand = operand->sibling;
        Value* rhs = emit_subexpr(operand, ANONYMOUS_VALUE, symbols);

        SubString value_name;
        if (symbol_id != ANONYMOUS_VALUE)
        {
            value_name = symbols.symbol_data->data[symbol_id].name;
        }

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

    Value* emit_subexpr(ASTNode* subexpr, uint32_t symbol_id, SymbolTable symbols)
    {
        Value* result;
        switch (subexpr->type)
        {
            case ASTNodeType::Identifier:
                result = symbols.values[subexpr->symbol_id];
                break;
            case ASTNodeType::Number:
                result = ConstantInt::get(IntegerType::get(llvm_ctxt, 32), subexpr->value);
                break;
            case ASTNodeType::BinaryOperator:
                result = emit_binop(subexpr, symbol_id, symbols);
                break;
            default:
                assert(false && "Invalid syntax tree - expected a subexpression");
        }

        if (symbol_id != ANONYMOUS_VALUE)
        {
            symbols.values[symbol_id] = result;
        }
        return result;
    }

    void emit_statement(ASTNode* statement, SymbolTable symbols)
    {
        switch (statement->type)
        {
            case ASTNodeType::VariableDef:
            case ASTNodeType::Assignment:
            {
                emit_subexpr(statement->child, statement->symbol_id, symbols);
            } break;
            case ASTNodeType::FunctionDef:
                // do nothing
                break;
            case ASTNodeType::Return:
            {
                Value* ret_value = emit_subexpr(statement->child, ANONYMOUS_VALUE, symbols);
                ir_builder.CreateRet(ret_value);
            } break;
            default:
                assert(false && "Invalid syntax tree - expected a statement");
        }
    }

    void generate_function_def(ASTNode* function_def_node, SymbolTable symbols)
    {
        assert(function_def_node->child && function_def_node->child->type == ASTNodeType::ParameterList);

        std::vector<Type*> arg_types;
        FunctionType* function_type = FunctionType::get(Type::getVoidTy(llvm_ctxt), arg_types, false);
        Function* function = Function::Create(
            function_type,
            Function::ExternalLinkage,
            make_twine(symbols.symbol_data->data[function_def_node->symbol_id].name),
            module
        );

        ASTNode* statement_list = function_def_node->child->sibling;
        assert(statement_list && statement_list->type == ASTNodeType::StatementList);

        BasicBlock* entry = BasicBlock::Create(llvm_ctxt, "entry", function);
        ir_builder.SetInsertPoint(entry);

        ASTNode* statement = statement_list->child;

        SymbolTable statement_list_symbols;
        statement_list_symbols.symbol_data = statement_list->symbols;
        statement_list_symbols.values = new Value*[statement_list->symbols->size];

        while(statement)
        {
            emit_statement(statement, statement_list_symbols);
            statement = statement->sibling;
        }

        delete[] statement_list_symbols.values;
    }
};

void output_ast(Array<ASTNode, MAX_AST_SIZE>* ast, Array<SymbolData, MAX_SYMBOLS>* symbols)
{
    CodeEmitter emitter;

    SymbolTable symbol_table;
    symbol_table.symbol_data = symbols;
    symbol_table.values = new Value*[symbols->size];

    ASTNode* node = &ast->data[0];
    while (node)
    {
        switch (node->type)
        {
            case ASTNodeType::FunctionDef:
                emitter.generate_function_def(node, symbol_table);
                break;
            default:
                assert(false);  // unsupported
        }

        node = node->sibling;
    }

    delete[] symbol_table.values;
    
    emitter.module->print(errs(), nullptr);
}
