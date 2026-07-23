CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O0 -g
SRC = ../components/lux_chigo_ir/protocol.cpp
TEST = test_protocol.cpp

test_protocol: $(TEST) $(SRC) ../components/lux_chigo_ir/protocol.h
	$(CXX) $(CXXFLAGS) -o $@ $(TEST) $(SRC)

.PHONY: run
run: test_protocol fixtures.json
	./test_protocol fixtures.json

.PHONY: clean
clean:
	rm -f test_protocol
