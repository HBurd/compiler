#include <fstream>
#include <cassert>

#include "report_error.h"

#include "lexer.h"
#include "parser.h"
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

    // generate parse tree
    auto symbols = new Array<SymbolData, MAX_SYMBOLS>;
    auto ast = new Array<ASTNode, MAX_AST_SIZE>;
    parse(tokens, ast, symbols);

    output_ast(ast, symbols);
}
