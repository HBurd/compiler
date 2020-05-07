#include <fstream>
#include <cassert>

#include "report_error.h"

#include <iostream>

#include "lexer.h"
#include "parser.h"
#include "type_check.h"
#include "codegen_llvm.h"

int main(int argc, char **argv)
{
    assert(argc == 2);

    // read in file
    std::ifstream file(argv[1], std::ios_base::in | std::ios_base::ate);
    size_t len = file.tellg();

    file.seekg(std::ios_base::beg);
    
    char *file_contents = new char[len + 1];
    file_contents[len] = 0;

    file.read(file_contents, len);

    init_error_reporting(file_contents);

    // get tokens
    std::vector<Token> tokens;
    lex(file_contents, tokens);

    // generate AST
    AST ast;

    Array<SymbolData> symbols;
    symbols.max_length = MAX_SYMBOLS;
    symbols.data = new SymbolData[MAX_SYMBOLS];

    parse(tokens, ast, symbols);

    check_types(ast);

    output_ast(ast, symbols);
}
