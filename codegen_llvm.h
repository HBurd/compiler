#pragma once
#include "util.h"
#include "parser.h"

void output_ast(Array<ASTNode, MAX_AST_SIZE>* ast, Array<SymbolData, MAX_SYMBOLS>* symbols);
