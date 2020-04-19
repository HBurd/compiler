CPPFLAGS = -MMD -Wall -Wextra -g
CXXFLAGS = -std=c++11
objects = compiler.o lexer.o parser.o

CXX = clang++

default: compiler

clean:
	rm -f compiler *.o *.d

compiler: $(objects)
	$(CXX) $(objects) -o compiler $(CXXFLAGS) $(CPPFLAGS)

test: compiler
	-./compiler test.hb

-include *.d
