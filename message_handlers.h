#ifndef KAYLES_MESSAGE_HANDLERS_H
#define KAYLES_MESSAGE_HANDLERS_H

#include <netinet/in.h>

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "game_logic.h"

void handle_join_game(const std::string& buffer, const std::vector<std::string>& parts, std::unordered_map<uint32_t, GameState>& active_games,
    const GameState& template_game, int socket_fd, const struct sockaddr_in& client_addr);

void handle_make_move_one(const std::string& buffer, const std::vector<std::string>& parts, std::unordered_map<uint32_t, GameState>& active_games,
    const GameState& /*template_game*/, int socket_fd, const struct sockaddr_in& client_addr);

void handle_make_move_two(const std::string& buffer, const std::vector<std::string>& parts, std::unordered_map<uint32_t, GameState>& active_games,
    const GameState& /*template_game*/, int socket_fd, const struct sockaddr_in& client_addr);

void handle_give_up(const std::string& buffer, const std::vector<std::string>& parts, std::unordered_map<uint32_t, GameState>& active_games,
    const GameState& /*template_game*/, int socket_fd, const struct sockaddr_in& client_addr);

void handle_keep_alive(const std::string& buffer, const std::vector<std::string>& parts, std::unordered_map<uint32_t, GameState>& active_games,
    const GameState& /*template_game*/, int socket_fd, const struct sockaddr_in& client_addr);

void handle_wrong_message(const std::string& buffer, uint8_t error_index, int socket_fd, const struct sockaddr_in& client_addr);

#endif //KAYLES_MESSAGE_HANDLERS_H
