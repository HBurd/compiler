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

struct SymbolTable
{
    Array<SymbolData> symbol_data;
    llvm::Value** values;
};

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

    static constexpr uint32_t ANONYMOUS_VALUE = 0xFFFFFFFF;

    CodeEmitter()
    :ir_builder(llvm_ctxt)
    {
        module = new llvm::Module("top", llvm_ctxt);
    }

    ~CodeEmitter()
    {
        delete module;
    }

    llvm::Value* emit_binop(ASTBinOpNode* subexpr, uint32_t symbol_id, SymbolTable symbols)
    {
        ASTNode* operand = subexpr->child;
        llvm::Value* lhs = emit_subexpr(operand, ANONYMOUS_VALUE, symbols);
        llvm::Value* rhs = emit_subexpr(operand->sibling, ANONYMOUS_VALUE, symbols);

        SubString value_name;
        if (symbol_id != ANONYMOUS_VALUE)
        {
            value_name = symbols.symbol_data[symbol_id].name;
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

    llvm::Value* emit_subexpr(ASTNode* subexpr, uint32_t symbol_id, SymbolTable symbols)
    {
        llvm::Value* result;
        switch (subexpr->type)
        {
            case ASTNodeType::Identifier:
                result = symbols.values[static_cast<ASTIdentifierNode*>(subexpr)->symbol_id];
                break;
            case ASTNodeType::Number:
                // TODO: after type checking the number will actually have a type, so don't hardcode
                result = llvm::ConstantInt::get(get_integer_type(TypeId::U32, llvm_ctxt), static_cast<ASTNumberNode*>(subexpr)->value);
                break;
            case ASTNodeType::BinaryOperator:
                result = emit_binop(static_cast<ASTBinOpNode*>(subexpr), symbol_id, symbols);
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
                emit_subexpr(statement->child, static_cast<ASTIdentifierNode*>(statement)->symbol_id, symbols);
            } break;
            case ASTNodeType::FunctionDef:
                // do nothing
                break;
            case ASTNodeType::Return:
            {
                llvm::Value* ret_value = emit_subexpr(statement->child, ANONYMOUS_VALUE, symbols);
                ir_builder.CreateRet(ret_value);
            } break;
            case ASTNodeType::If:
            {
                llvm::Function* function = ir_builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* then_block = llvm::BasicBlock::Create(llvm_ctxt, "then", function);
                llvm::BasicBlock* else_block = llvm::BasicBlock::Create(llvm_ctxt, "else", function);
                llvm::BasicBlock* fi_block = llvm::BasicBlock::Create(llvm_ctxt, "end_if", function);

                llvm::Value* condition_value = emit_subexpr(statement->child, ANONYMOUS_VALUE, symbols);
                ir_builder.CreateCondBr(condition_value, then_block, else_block);

                ir_builder.SetInsertPoint(then_block);
                emit_statement_list(statement->child->sibling, symbols);
                ir_builder.CreateBr(fi_block);
                ir_builder.SetInsertPoint(fi_block);
            } break;
            case ASTNodeType::While:
            {
                llvm::Function* function = ir_builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* do_block = llvm::BasicBlock::Create(llvm_ctxt, "do", function);
                llvm::BasicBlock* fi_block = llvm::BasicBlock::Create(llvm_ctxt, "end_do", function);

                llvm::Value* condition_value = emit_subexpr(statement->child, ANONYMOUS_VALUE, symbols);
                ir_builder.CreateCondBr(condition_value, do_block, fi_block);

                ir_builder.SetInsertPoint(do_block);
                emit_statement_list(statement->child->sibling, symbols);
                llvm::Value* end_condition_value = emit_subexpr(statement->child, ANONYMOUS_VALUE, symbols);
                ir_builder.CreateCondBr(end_condition_value, do_block, fi_block);
                ir_builder.SetInsertPoint(fi_block);
            } break;
            default:
                assert(false && "Invalid syntax tree - expected a statement");
        }
    }

    void emit_statement_list(ASTNode* statement_list, SymbolTable symbols)
    {
        assert(statement_list->type == ASTNodeType::StatementList);

        ASTNode* statement = statement_list->child;

        while(statement)
        {
            emit_statement(statement, symbols);
            statement = statement->sibling;
        }
    }

    void generate_function_def(ASTNode* function_def_node, SymbolTable symbols)
    {
        ASTNode* parameter_list = function_def_node->child;
        assert(parameter_list && parameter_list->type == ASTNodeType::ParameterList);

        ASTStatementListNode* statement_list = static_cast<ASTStatementListNode*>(function_def_node->child->sibling);
        assert(statement_list && statement_list->type == ASTNodeType::StatementList);

        SymbolTable function_symbols;
        function_symbols.symbol_data = statement_list->symbols;
        function_symbols.values = new llvm::Value*[statement_list->symbols.length];

        std::vector<llvm::Type*> arg_types;
        {
            ASTNode* parameter = parameter_list->child;
            while (parameter)
            {
                arg_types.push_back(get_integer_type(function_symbols.symbol_data[static_cast<ASTIdentifierNode*>(parameter)->symbol_id].type_id, llvm_ctxt));
                parameter = parameter->sibling;
            }
        }

        llvm::FunctionType* function_type = llvm::FunctionType::get(get_integer_type(TypeId::U32, llvm_ctxt), arg_types, false);
        llvm::Function* function = llvm::Function::Create(
            function_type,
            llvm::Function::ExternalLinkage,
            make_twine(symbols.symbol_data[static_cast<ASTIdentifierNode*>(function_def_node)->symbol_id].name),
            module
        );

        // set function arg names and values
        {
            ASTIdentifierNode* parameter = static_cast<ASTIdentifierNode*>(function_def_node->child->child);
            auto arg = function->arg_begin();
            while (parameter)
            {
                assert(arg != function->arg_end());

                arg->setName(make_twine(function_symbols.symbol_data[parameter->symbol_id].name));
                function_symbols.values[parameter->symbol_id] = &(*arg);  // convert iterator to pointer

                parameter = static_cast<ASTIdentifierNode*>(parameter->sibling);
                ++arg;
            }
        }

        llvm::BasicBlock* entry = llvm::BasicBlock::Create(llvm_ctxt, "entry", function);
        ir_builder.SetInsertPoint(entry);

        emit_statement_list(statement_list, function_symbols);

        delete[] function_symbols.values;

        llvm::verifyFunction(*function);
    }
};

void output_ast(AST& ast, Array<SymbolData> symbols)
{
    CodeEmitter emitter;

    SymbolTable symbol_table;
    symbol_table.symbol_data = symbols;
    symbol_table.values = new llvm::Value*[symbols.length];

    ASTNode* node = ast.start;
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
    
    emitter.module->print(llvm::errs(), nullptr);
}
