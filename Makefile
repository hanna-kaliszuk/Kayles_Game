CXX = g++
CXXFLAGS = -Wall -Wextra -Werror -Wsign-conversion -std=c++17

TARGETS = kayles_server kayles_client

all: $(TARGETS)

kayles_server: kayles_server.o common.o err.o game_logic.o message_handlers.o
	$(CXX) $(CXXFLAGS) -o $@ $^

kayles_client: kayles_client.o common.o err.o game_logic.o client_messages.o
	$(CXX) $(CXXFLAGS) -o $@ $^

kayles_server.o: kayles_server.cpp common.h err.h game_logic.h message_handlers.h
	$(CXX) $(CXXFLAGS) -c $<

kayles_client.o: kayles_client.cpp common.h err.h game_logic.h client_messages.h
	$(CXX) $(CXXFLAGS) -c $<

message_handlers.o: message_handlers.cpp message_handlers.h common.h err.h game_logic.h
	$(CXX) $(CXXFLAGS) -c $<

game_logic.o: game_logic.cpp game_logic.h
	$(CXX) $(CXXFLAGS) -c $<

common.o: common.cpp common.h err.h
	$(CXX) $(CXXFLAGS) -c $<

err.o : err.cpp err.h
	$(CXX) $(CXXFLAGS) -c $<

client_messages.o: client_messages.cpp client_messages.h common.h err.h game_logic.h
	$(CXX) $(CXXFLAGS) -c $<

clean:
	rm -f *.o $(TARGETS)

.PHONY: all clean
