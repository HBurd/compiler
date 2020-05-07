CPPFLAGS = -MMD -Wall -Wextra -g
CXXFLAGS = -std=c++11
objects = compiler.o lexer.o parser.o report_error.o codegen_llvm.o type_check.o

CXX = clang++

default: compiler

clean:
	rm -f compiler *.o *.d

codegen_llvm.o: codegen_llvm.cc
	$(CXX) codegen_llvm.cc $(CXXFLAGS) $(CPPFLAGS) -Wno-unused-parameter -I`llvm-config --includedir` -c

compiler: $(objects)
	$(CXX) $(objects) -o compiler $(CXXFLAGS) $(CPPFLAGS) `llvm-config --ldflags --system-libs --libs core`

test: compiler
	-./compiler test.hb

-include *.d
