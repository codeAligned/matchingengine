#default Makefile

CXX=g++
CXXFLAGS=-std=c++11
BIN=matchingengine

SRC=$(wildcard *.cpp)
OBJ=$(SRC:.cpp=.o)

all: $(OBJ)
	$(CXX) -o $(BIN) $^

%.o: %.c
	$(CXX) $@ -c $<

clean:
	rm -f $(OBJ)
	rm -f $(BIN) 
