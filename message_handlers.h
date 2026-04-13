#ifndef KAYLES_MESSAGE_HANDLERS_H
#define KAYLES_MESSAGE_HANDLERS_H

#include <netinet/in.h>

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "game_logic.h"

//  MSG_JOIN       (0): [type:1][player_id:4]                     = 5 B
//  MSG_MOVE_1     (1): [type:1][player_id:4][game_id:4][pawn:4]  = 13 B
//  MSG_MOVE_2     (2): [type:1][player_id:4][game_id:4][pawn:4]  = 13 B
//  MSG_KEEP_ALIVE (3): [type:1][player_id:4][game_id:4]          = 9 B
//  MSG_GIVE_UP    (4): [type:1][player_id:4][game_id:4]          = 9 B

constexpr size_t JOIN_SIZE        = 1u + 4u;
constexpr size_t MOVE_SIZE        = 1u + 4u + 4u + 1u;
constexpr size_t KEEP_ALIVE_SIZE  = 1u + 4u + 4u;
constexpr size_t GIVE_UP_SIZE     = 1u + 4u + 4u;

constexpr size_t OFF_PLAYER_ID = 1u;   // bajty 1-4
constexpr size_t OFF_GAME_ID   = 5u;   // bajty 5-8
constexpr size_t OFF_PAWN_IDX  = 9u;   // bajty 9-12

//  buf  — surowy bufor binarny odebrany z sieci
//  len  — liczba odebranych bajtów


void handle_join_game(const char* buf, size_t len,
                      std::unordered_map<uint32_t, GameState>& active_games,
                      const GameState& template_game,
                      int socket_fd, const struct sockaddr_in& client_addr);

void handle_make_move_one(const char* buf, size_t len,
                          std::unordered_map<uint32_t, GameState>& active_games,
                          const GameState& template_game,
                          int socket_fd, const struct sockaddr_in& client_addr);

void handle_make_move_two(const char* buf, size_t len,
                          std::unordered_map<uint32_t, GameState>& active_games,
                          const GameState& template_game,
                          int socket_fd, const struct sockaddr_in& client_addr);

void handle_give_up(const char* buf, size_t len,
                    std::unordered_map<uint32_t, GameState>& active_games,
                    const GameState& template_game,
                    int socket_fd, const struct sockaddr_in& client_addr);

void handle_keep_alive(const char* buf, size_t len,
                       std::unordered_map<uint32_t, GameState>& active_games,
                       const GameState& template_game,
                       int socket_fd, const struct sockaddr_in& client_addr);

void handle_wrong_message(const char* buf, size_t len,
                          uint8_t error_index,
                          int socket_fd, const struct sockaddr_in& client_addr);

#endif //KAYLES_MESSAGE_HANDLERS_H
