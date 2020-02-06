CPPFLAGS = -MMD
CXXFLAGS = -std=c++11
objects = compiler.o lexer.o parser.o

default: compiler

clean:
	rm -f compiler *.o *.d

compiler: $(objects)
	g++ $(objects) -o compiler $(CXXFLAGS) $(CPPFLAGS)

test: compiler
	./compiler test.hb

-include *.d
