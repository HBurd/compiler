#include "parser.h"
#include <iostream>

struct FunctionDeclarationNode
{
    DeclarationNode *next;
};

// For parse tree
struct FileNode
{
    DeclarationNode *children;
};

void parse(const std::vector<Token>& tokens)
{
    
}
