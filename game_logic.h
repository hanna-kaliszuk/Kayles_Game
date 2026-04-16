/**
 * @file game_logic.h
 * @brief Core game logic, state management and bitwise board operations for Kayles game. 
**/

#ifndef KAYLES_GAME_LOGIC_H
#define KAYLES_GAME_LOGIC_H

#include <cstdint>
#include <string>
#include <vector>

constexpr uint8_t WAITING_FOR_OPPONENT = 0;
constexpr uint8_t TURN_A = 1;
constexpr uint8_t TURN_B = 2;
constexpr uint8_t WIN_A = 3;
constexpr uint8_t WIN_B = 4;

/**
 * @brief Represents the current state of a single Kayles game session. 
 * 
 * Player ID: unique, non-negative integers
 * Status: current state of the game: 
 *  - WAITING_FOR_OPONENET (0)  - waiting for player B to join 
 *  - TURN_A (1)                - waiting for player A to make a move
 *  - TURN_B (2)                - waiting for player B to make a move
 *  - WIN_A (3)                 - player A won
 *  - WIN_B (4)                 - player B won
 * Max_pawn: max pawn index
 * Pawn_row: bit map representing pawns
 * Last_activity: time of the last activity within the session 
 * 
**/
struct GameState {
    uint32_t player_a_id;       
    uint32_t player_b_id;      
    uint8_t status;             
    uint8_t max_pawn;
    std::vector<uint8_t> pawn_row;
    time_t last_activity;
};

/**
 * @brief Parses a binary string of '0's and '1's and initializes the game's bitset board. 
 * 
 * @param str_pawns the string representation of the board
 * @param game the game state object 
 * 
 * @note The str_pawn must not contain any other symbols that '0' and '1'
 * 
**/
void initialize_pawn_row(const std::string& str_pawns, GameState& game);

std::string serialize_pawn_row(const GameState& game);

void knock_pawn_down(GameState& game_state, uint32_t pawn_idx);

bool is_pawn_standing(GameState& game_state, uint32_t pawn_idx);

bool is_legal_move(GameState& game_state, uint32_t pawn_idx);

bool any_pawn_left(GameState& game);

uint8_t verify_game_state_after_move(GameState& game);

#endif //KAYLES_GAME_LOGIC_H
