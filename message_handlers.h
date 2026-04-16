/**
 * @file message_handlers.h
 * @brief Message handling utilities to process incoming UDP binary messages on the server.
 * * Protocol definitions:
 * - MSG_JOIN (0): [type : 1][player_id : 4] = 5B
 * - MSG_MOVE_1 (1): [type : 1][player_id : 4][game_id : 4][pawn : 1] = 10B
 * - MSG_MOVE_2 (2): [type : 1][player_id : 4][game_id : 4][pawn : 1] = 13B
 * - MSG_KEEP_ALIVE (3): [type : 1][player_id : 4][game_id : 4] = 9B
 * - MSG_GIVE_UP (4): [type : 1][player_id : 4][game_id : 4]  = 9B
**/

#ifndef KAYLES_MESSAGE_HANDLERS_H
#define KAYLES_MESSAGE_HANDLERS_H

#include <netinet/in.h>

#include <cstdint>
#include <unordered_map>

#include "game_logic.h"

constexpr size_t JOIN_SIZE = 5u;
constexpr size_t MOVE_SIZE = 10u;
constexpr size_t KEEP_ALIVE_SIZE = 9u;
constexpr size_t GIVE_UP_SIZE = 9u;
constexpr size_t OFF_PLAYER_ID = 1u; // bytes 1-4
constexpr size_t OFF_GAME_ID = 5u; // bytes 5-8
constexpr size_t OFF_PAWN_IDX = 9u; // bytes 9-12

/**
 * @brief Handles a player requesting to join a game.
 *
 * If there is a game waiting for an opponent, the player is added to it. Otherwise, a new game is created and marked
 * as waiting for opponent.
 *
 * Validates the incoming message format. Invalid messages result in an error response being sent to the client.
 *
 * On success, the updated game state is sent back to the requesting client.
 *
 * @param buf pointer to the raw binary buffer received from the network
 * @param len size of the received buffer in bytes (must match JOIN_SIZE)
 * @param active_games the map of currently active games indexed by game ID
 * @param template_game a template representing a brand-new game state
 * @param socket_fd server socket file descriptor
 * @param client_addr the address structure of the client that sent the request
 *
 * @note Player_id must be non-zero.
 * @note This function may create a new game or modify an existing one.
 * @note In case of resource exhaustion, the request is silently ignored.
**/
void handle_join_game(const char* buf, size_t len, std::unordered_map<uint32_t, GameState>& active_games,
                      const GameState& template_game, int socket_fd, const struct sockaddr_in& client_addr);

/**
 * @brief Handles a player requesting to knock down a single pawn.
 *
 * Validates the incoming message format and extracts the player_id, game_id and target pawn index. The function
 * verifies that:
 * - the referenced game exists & the player is a registered participant,
 * - it is the player's turn,
 * - the requested move is legal.
 *
 * If all validations pass, the pawn is knocked down and the game state is updated accordingly.
 *
 * In all cases the current game state is sent to the client. Malformed messages result in an error response.
 *
 *
 * @param buf raw binary buffer received from the network
 * @param len number of bytes received
 * @param active_games map of currently active games
 * @param template_game a template representing a brand-new game state
 * @param socket_fd server socket file descriptor
 * @param client_addr the address structure of the client that sent the request
 *
 * @note If the move is invalid, or it is not the player's turn, the game state is returned unchanged.
**/
void handle_make_move_one(const char* buf, size_t len, std::unordered_map<uint32_t, GameState>& active_games,
                          const GameState& template_game, int socket_fd, const struct sockaddr_in& client_addr);

/**
 * @brief Handles a player requesting to knock down two adjacent pawns.
 *
 * Validates the incoming message format and extracts the player_id, game_id and target pawn index. The move targets two
 * consecutive pawns: the pawn at the given index and the next one (index + 1). The function verifies that:
 * - the referenced game exists & the player is a registered participant,
 * - it is the player's turn,
 * - the requested move is legal.
 *
 * If all validations pass, both pawns are knocked down and the game state is updated accordingly.
 *
 * In all cases the current game state is sent to the client. Malformed messages result in an error response.
 *
 * @param buf raw binary buffer received from the network
 * @param len number of bytes received
 * @param active_games map of currently active games
 * @param socket_fd server socket file descriptor
 * @param client_addr the address structure of the client that sent the request
 *
 * @note If the move is invalid, or it is not the player's turn, the game state is returned unchanged.
**/
void handle_make_move_two(const char* buf, size_t len, std::unordered_map<uint32_t, GameState>& active_games,
                          const GameState& template_game, int socket_fd, const struct sockaddr_in& client_addr);

/**
 * @brief Handles a player requesting to forfeit the game.
 *
 * Validates the incoming message format and extracts the player_id, game_id.
 *
 * The function verifies that:
 * - the referenced game exists & the player is a registered participant,
 * - it is the player's turn.
 *
 * If all validations pass, the game status is updated and sent to the requesting client. Malformed messages result in
 * an error response.
 *
 * @param buf raw binary buffer received from the network
 * @param len number of bytes received
 * @param active_games map of currently active games
 * @param template_game a template representing a brand-new game state
 * @param socket_fd server socket file descriptor
 * @param client_addr the address structure of the client that sent the request
 *
 * @note If the move is invalid, or it is not the player's turn, the game state is returned unchanged.
**/
void handle_give_up(const char* buf, size_t len, std::unordered_map<uint32_t, GameState>& active_games,
                    const GameState& template_game, int socket_fd, const struct sockaddr_in& client_addr);

/**
 * @brief Handles a player requesting to prevent game timeout.
 *
 * Validates the incoming message format and extracts the player_id, game_id.
 *
 * The function verifies that:
 * - the referenced game exists & the player is a registered participant,
 * - it is the player's turn.
 *
 * If all validations pass, the game status is updated and sent to the requesting client. Malformed messages result in
 * an error response.
 *
 * @param buf raw binary buffer received from the network
 * @param len number of bytes received
 * @param active_games map of currently active games
 * @param template_game a template representing a brand-new game state
 * @param socket_fd server socket file descriptor
 * @param client_addr the address structure of the client that sent the request
**/
void handle_keep_alive(const char* buf, size_t len, std::unordered_map<uint32_t, GameState>& active_games,
                       const GameState& template_game, int socket_fd, const struct sockaddr_in& client_addr);

/**
 * @brief Handles responding to the client with an error if the incoming message was malformed.
 *
 * This function echoes up to the first 12 bytes of the original request and appends an error status code along * with the specific byte offset where the parsing error was detected. 
 * 
 * @param buf raw binary buffer received from the network
 * @param len number of bytes received
 * @param error_index the byte offset where the error was detected
 * @param socket_fd server socket file descriptor
 * @param client_addr the address structure of the client that sent the request
**/
void handle_wrong_message(const char* buf, size_t len, uint8_t error_index, int socket_fd,
                          const struct sockaddr_in& client_addr);

#endif //KAYLES_MESSAGE_HANDLERS_H
