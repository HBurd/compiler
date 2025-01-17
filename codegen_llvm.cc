#include "codegen.h"
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

static llvm::Type* get_type(uint32_t type_id, llvm::LLVMContext& llvm_ctxt)
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
        case TypeId::Pointer:
            return llvm::Type::getInt8PtrTy(llvm_ctxt);
        case TypeId::None:
            return llvm::Type::getVoidTy(llvm_ctxt);
        default:
            return nullptr;
    }
}

struct PhiNode
{
    llvm::Value* original_value = nullptr;
    llvm::Value* new_value = nullptr;
    SymbolData* symbol = nullptr;
    llvm::PHINode* llvm_phi = nullptr;
    PhiNode* parent_phi = nullptr;
};

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

    llvm::Value* emit_call(ASTNode* call_node, SymbolData* symbol)
    {
        assert(call_node->type == ASTNodeType::FunctionCall);

        llvm::Function* function = module->getFunction(make_twine(static_cast<ASTIdentifierNode*>(call_node->child)->symbol->name));
        assert(function && "Unknown function referenced");

        std::vector<llvm::Value*> arg_values;

        ASTNode* arg_node = call_node->child->sibling;
        while (arg_node)
        {
            arg_values.push_back(emit_subexpr(arg_node, nullptr));
            arg_node = arg_node->sibling;
        }

        assert((arg_values.size() == function->arg_size()) && "Incorrect number of arguments");

        // TODO: Need to check it symbol is null, right?
        return ir_builder.CreateCall(function, arg_values, make_twine(symbol->name));
    }

    llvm::Value* emit_string(ASTStringNode* string, SymbolData* symbol)
    {
        // Copy and null terminate
        char* new_str = (char*)malloc(string->str.len + 1);
        memcpy(new_str, string->str.start, string->str.len);
        new_str[string->str.len] = 0;

        SubString new_substr;
        new_substr.start = new_str;
        new_substr.len = string->str.len + 1;

        SubString value_name;
        if (symbol)
        {
            value_name = symbol->name;
        }

        return ir_builder.CreateGlobalStringPtr(make_twine(new_substr), make_twine(value_name));
    }

    llvm::Value* emit_subexpr(ASTNode* subexpr, SymbolData* symbol)
    {
        llvm::Value* result;
        switch (subexpr->type)
        {
            case ASTNodeType::Identifier:
                result = (llvm::Value*)static_cast<ASTIdentifierNode*>(subexpr)->symbol->codegen_data->new_value;
                break;
            case ASTNodeType::Number:
                // TODO: after type checking the number will actually have a type, so don't hardcode
                result = llvm::ConstantInt::get(get_type(TypeId::U32, llvm_ctxt), static_cast<ASTNumberNode*>(subexpr)->value);
                break;
            case ASTNodeType::BinaryOperator:
                result = emit_binop(static_cast<ASTBinOpNode*>(subexpr), symbol);
                break;
            case ASTNodeType::FunctionCall:
                result = emit_call(subexpr, symbol);
                break;
            case ASTNodeType::String:
                result = emit_string(static_cast<ASTStringNode*>(subexpr), symbol);
                break;
            default:
                assert(false && "Invalid syntax tree - expected a subexpression");
        }

        return result;
    }

    void emit_variable_def(ASTIdentifierNode* identifier_node, Array<PhiNode>* phi_nodes)
    {
        SymbolData* symbol = identifier_node->symbol;
        PhiNode new_phi;
        new_phi.symbol = symbol;
        new_phi.new_value = emit_subexpr(identifier_node->child, symbol);;

        // Note: It is correct to leave original_value as default,
        // since it should only be set to the value from BEFORE block.

        symbol->codegen_data = phi_nodes->push(new_phi);
    }

    void emit_statement(ASTNode* statement, Array<PhiNode>* phi_nodes)
    {
        switch (statement->type)
        {
            case ASTNodeType::VariableDef:
            {
                emit_variable_def(static_cast<ASTIdentifierNode*>(statement), phi_nodes);
            } break;
            case ASTNodeType::Assignment:
            {
                SymbolData* symbol = static_cast<ASTIdentifierNode*>(statement)->symbol;
                symbol->codegen_data->new_value = emit_subexpr(statement->child, symbol);
            } break;
            case ASTNodeType::FunctionDef:
                // do nothing
                break;
            case ASTNodeType::Return:
            {
                if (statement->child)
                {
                    llvm::Value* ret_value = emit_subexpr(statement->child, nullptr);
                    ir_builder.CreateRet(ret_value);
                }
                else
                {
                    // Not returning a value;
                    ir_builder.CreateRetVoid();
                }
            } break;
            case ASTNodeType::If:
            {
                bool has_else = (bool)statement->child->sibling->sibling;

                llvm::BasicBlock* before_block = ir_builder.GetInsertBlock();
                llvm::Function* function = before_block->getParent();

                llvm::BasicBlock* then_block = llvm::BasicBlock::Create(llvm_ctxt, "then", function);
                llvm::BasicBlock* else_block = has_else ? llvm::BasicBlock::Create(llvm_ctxt, "else", function) : nullptr;
                llvm::BasicBlock* fi_block = llvm::BasicBlock::Create(llvm_ctxt, "end_if", function);

                if (!else_block) else_block = fi_block;

                llvm::Value* condition_value = emit_subexpr(statement->child, nullptr);
                ir_builder.CreateCondBr(condition_value, then_block, else_block);

                // Create new phi nodes

                size_t phi_frame_base = phi_nodes->length;
                ir_builder.SetInsertPoint(fi_block);

                for (size_t i = 0; i < phi_frame_base; ++i)
                {
                    PhiNode* phi = &(*phi_nodes)[i];
                    PhiNode* new_phi = phi_nodes->push(*phi);
                    new_phi->original_value = new_phi->new_value;
                    new_phi->parent_phi = phi;
                    new_phi->llvm_phi = ir_builder.CreatePHI(get_type(new_phi->symbol->type_id, llvm_ctxt), 2, make_twine(new_phi->symbol->name));

                    // The value of the phi node itself is the new value
                    // for the symol.
                    phi->new_value = new_phi->llvm_phi;

                    // Point symbol to new phi node
                    new_phi->symbol->codegen_data = new_phi;
                }

                Array<PhiNode> inner_phi_nodes;
                inner_phi_nodes.data = phi_nodes->data + phi_frame_base;
                inner_phi_nodes.length = phi_nodes->length - phi_frame_base;
                inner_phi_nodes.max_length = phi_nodes->max_length - phi_frame_base;

                // Emit then

                ir_builder.SetInsertPoint(then_block);
                emit_statement_list(statement->child->sibling, inner_phi_nodes);
                ir_builder.CreateBr(fi_block);

                // Update then block, since it might have changed
                then_block = ir_builder.GetInsertBlock();

                // Update phi nodes
                for (PhiNode& phi : inner_phi_nodes)
                {
                    phi.llvm_phi->addIncoming(phi.new_value, then_block);
                    phi.new_value = phi.original_value;
                }

                if (has_else)
                {
                    // Emit else
                    ir_builder.SetInsertPoint(else_block);
                    emit_statement_list(statement->child->sibling->sibling, inner_phi_nodes);
                    ir_builder.CreateBr(fi_block);

                    // Update else block, since it might have changed
                    else_block = ir_builder.GetInsertBlock();

                    // Update phi nodes
                    for (PhiNode& phi : inner_phi_nodes)
                    {
                        phi.llvm_phi->addIncoming(phi.new_value, else_block);
                        phi.new_value = phi.original_value;
                    }
                }
                else
                {
                    // There is no else block.
                    for (PhiNode& phi : inner_phi_nodes)
                    {
                        phi.llvm_phi->addIncoming(phi.original_value, before_block);
                    }
                }

                // Point symbols back to their original phis
                for (PhiNode& phi : inner_phi_nodes)
                {
                    phi.symbol->codegen_data = phi.parent_phi;
                }

                ir_builder.SetInsertPoint(fi_block);
            } break;
            case ASTNodeType::While:
            {
                llvm::Function* function = ir_builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* before_block = ir_builder.GetInsertBlock();
                llvm::BasicBlock* do_block = llvm::BasicBlock::Create(llvm_ctxt, "do", function);
                llvm::BasicBlock* fi_block = llvm::BasicBlock::Create(llvm_ctxt, "end_do", function);

                llvm::Value* condition_value = emit_subexpr(statement->child, nullptr);
                ir_builder.CreateCondBr(condition_value, do_block, fi_block);

                // Create new phi nodes

                size_t phi_frame_base = phi_nodes->length;
                ir_builder.SetInsertPoint(do_block);

                for (size_t i = 0; i < phi_frame_base; ++i)
                {
                    PhiNode* phi = &(*phi_nodes)[i];
                    PhiNode* new_phi = phi_nodes->push(*phi);
                    new_phi->original_value = new_phi->new_value;
                    new_phi->parent_phi = phi;

                    // The llvm phi node has to be created up here so that is emitted in the right place
                    new_phi->llvm_phi = ir_builder.CreatePHI(get_type(new_phi->symbol->type_id, llvm_ctxt), 2, make_twine(new_phi->symbol->name));
                    new_phi->new_value = new_phi->llvm_phi;

                    // Point symbol to new phi node
                    new_phi->symbol->codegen_data = new_phi;
                }

                Array<PhiNode> inner_phi_nodes;
                inner_phi_nodes.data = phi_nodes->data + phi_frame_base;
                inner_phi_nodes.length = phi_nodes->length - phi_frame_base;
                inner_phi_nodes.max_length = phi_nodes->max_length - phi_frame_base;

                emit_statement_list(statement->child->sibling, inner_phi_nodes);

                do_block = ir_builder.GetInsertBlock();
                for (const PhiNode& phi : inner_phi_nodes)
                {
                    phi.llvm_phi->addIncoming(phi.original_value, before_block);
                    phi.llvm_phi->addIncoming(phi.new_value, do_block);
                }

                llvm::Value* end_condition_value = emit_subexpr(statement->child, nullptr);
                ir_builder.CreateCondBr(end_condition_value, do_block, fi_block);

                ir_builder.SetInsertPoint(fi_block);

                for (const PhiNode& phi : inner_phi_nodes)
                {
                    llvm::PHINode* llvm_phi = ir_builder.CreatePHI(get_type(phi.symbol->type_id, llvm_ctxt), 2, make_twine(phi.symbol->name));

                    llvm_phi->addIncoming(phi.original_value, before_block);
                    llvm_phi->addIncoming(phi.new_value, do_block);

                    phi.parent_phi->new_value = llvm_phi;
                    phi.symbol->codegen_data = phi.parent_phi;
                }
            } break;
            default:
            {
                // This must be an expression-statement
                
                // Create empty symbol since value is unused
                // TODO: This is messy
                SymbolData empty_symbol;
                emit_subexpr(statement, &empty_symbol);
            }
        }
    }

    // Takes phi_nodes by value because we don't want any new symbols propagating back
    // to the outer scope
    void emit_statement_list(ASTNode* statement_list, Array<PhiNode> phi_nodes)
    {
        assert(statement_list->type == ASTNodeType::StatementList);

        ASTNode* statement = statement_list->child;

        while(statement)
        {
            emit_statement(statement, &phi_nodes);
            statement = statement->sibling;
        }
    }

    void generate_function_def(ASTNode* function_def_node)
    {
        ASTNode* parameter_list = function_def_node->child;
        assert(parameter_list && parameter_list->type == ASTNodeType::ParameterList);

        std::vector<llvm::Type*> arg_types;
        {
            ASTNode* parameter = parameter_list->child;
            while (parameter)
            {
                arg_types.push_back(get_type(static_cast<ASTIdentifierNode*>(parameter)->symbol->type_id, llvm_ctxt));
                parameter = parameter->sibling;
            }
        }

        llvm::FunctionType* function_type = llvm::FunctionType::get(get_type(static_cast<ASTIdentifierNode*>(function_def_node)->symbol->function_info->return_type, llvm_ctxt), arg_types, false);
        llvm::Function* function = llvm::Function::Create(
            function_type,
            llvm::Function::ExternalLinkage,
            make_twine(static_cast<ASTIdentifierNode*>(function_def_node)->symbol->name),
            module
        );

        Array<PhiNode> phi_nodes;
        phi_nodes.data = new PhiNode[MAX_SYMBOLS];
        phi_nodes.length = 0;
        phi_nodes.max_length = MAX_SYMBOLS;

        // set function arg names and values
        {
            ASTIdentifierNode* parameter = static_cast<ASTIdentifierNode*>(function_def_node->child->child);
            auto arg = function->arg_begin();
            while (parameter)
            {
                assert(arg != function->arg_end());

                arg->setName(make_twine(parameter->symbol->name));

                PhiNode new_phi;
                new_phi.symbol = parameter->symbol;
                new_phi.new_value = &(*arg);    // convert iterator to pointer

                parameter->symbol->codegen_data = phi_nodes.push(new_phi);

                parameter = static_cast<ASTIdentifierNode*>(parameter->sibling);
                ++arg;
            }
        }

        ASTStatementListNode* statement_list = static_cast<ASTStatementListNode*>(function_def_node->child->sibling);

        if (statement_list)
        {
            assert(statement_list->type == ASTNodeType::StatementList);

            llvm::BasicBlock* entry = llvm::BasicBlock::Create(llvm_ctxt, "entry", function);
            ir_builder.SetInsertPoint(entry);

            emit_statement_list(statement_list, phi_nodes);

            llvm::verifyFunction(*function);
        }
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

    llvm::verifyModule(*emitter.module);

    emitter.module->print(llvm::errs(), nullptr);
}
