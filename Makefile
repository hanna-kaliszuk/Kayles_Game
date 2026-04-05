CXX = g++
CXXFLAGS = -Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow -Wsign-conversion -std=c++17

TARGETS = kayles_server kayles_client

all: $(TARGETS)

kayles_server: kayles_server.o common.o
	$(CXX) $(CXXFLAGS) -o $@ $^

kayles_client: kayles_client.o common.o
	$(CXX) $(CXXFLAGS) -o $@ $^

kayles_server.o: kayles_server.cpp common.h err.h game_logic.h
	$(CXX) $(CXXFLAGS) -c $<

kayles_client.o: kayles_client.cpp common.h err.h
	$(CXX) $(CXXFLAGS) -c $<

common.o: common.cpp common.h err.h
	$(CXX) $(CXXFLAGS) -c $<

clean:
	rm -f *.o $(TARGETS)

.PHONY: all clean