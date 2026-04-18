/**
 * @file game_logic.h
 * @brief Core game logic, state management and bitwise board operations for Kayles game.
 *
 * ##### Board Representation #####
 * The pawn row is stored as a packed bit vector. Pawn indices are 0-based internally; the protocol uses 1-based indices.
 *
 * A bit value of 1 means the pawn is still standing; 0 means knocked down.
 *
 * ##### Game Status Constants #####
 *  - WAITING_FOR_OPPONENT (0)  - waiting for player B to join
 *  - TURN_A (1)                - waiting for player A to make a move
 *  - TURN_B (2)                - waiting for player B to make a move
 *  - WIN_A (3)                 - player A won
 *  - WIN_B (4)                 - player B won
**/

#ifndef KAYLES_GAME_LOGIC_H
#define KAYLES_GAME_LOGIC_H

#include <cstdint>
#include <string>
#include <vector>

// game status constants
constexpr uint8_t WAITING_FOR_OPPONENT = 0;
constexpr uint8_t TURN_A = 1;
constexpr uint8_t TURN_B = 2;
constexpr uint8_t WIN_A = 3;
constexpr uint8_t WIN_B = 4;

// board representation constants
constexpr char PAWN_STANDING = '1';
constexpr char PAWN_KNOCKED_DOWN = '0';

/**
 * @brief Represents the current state of a single Kayles game session. 
 * 
 * @field Player ID: unique, non-negative integers
 * @field Status: current state of the game:
 * @field Max_pawn: max pawn index
 * @field Pawn_row: bit map representing pawns
 * @field Last_activity: time of the last activity within the session
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
 * Each character in @p str_pawns maps directly to one pawn:
 * - '1' = pawn is standing
 * - '0' = pawn is knocked down
 *
 * The function also sets game.max_pawn = str_pawns.length() - 1 and allocates the pawn_row vector to the minimum number
 * of bytes required.
 * 
 * @param str_pawns the string representation of the board. Must not contain any characters other than '0' and '1'. Must
 * not be empty
 * @param game GameState whose pawn_row and max_pawn will be initialized.
 *
 * @note pawn_row validation should be performed before calling this function. Passing a string that contains other
 * characters produces undefined behavior.
**/
void initialize_pawn_row(const std::string& str_pawns, GameState& game);

/**
 * @brief Serializes the packed bitset board back into a human-readable binary string of '0's and '1's.
 *
 * The output is the string of the binary representation of the pawn row. Length of the returned string is
 * game.max_pawn + 1.
 *
 * @param game GameState whose pawn_row is to be serialized.
 * @return binary string of length (max_pawn + 1).
**/
std::string serialize_pawn_row(const GameState& game);

/**
 * @brief Knocks down the pawn at the given index by clearing its bit.
 *
 * Performs no bounds checking and no standing-state validation. Caller must ensure the move is legal (for example by
 * calling is_legal_move()) before calling this function.
 *
 * @param game_state session whose board is to be modified.
 * @param pawn_idx 0-based index of the pawn to knock down
 *
 * @warning Calling this on an out-of-range or already-knocked pawn produces undefined behavior.
**/
void knock_pawn_down(GameState& game_state, uint32_t pawn_idx);

/**
 * @brief Checks whether a specific pawn is still standing.
 *
 * @param game_state session to be checked.
 * @param pawn_idx 0-based index of the pawn to check
 * @return @c true if the pin's bit is set
 *         @c false if the pin's bit is not set
**/
bool is_pawn_standing(GameState& game_state, uint32_t pawn_idx);

/**
 * @brief Determined whether knocking down a given pawn constitutes a legal move.
 *
 * A move is legal if and only if:
 * 1. pawn_idx <= game_state.max_pawn
 * AND
 * 2. the pawn at pawn_idx is still standing.
 *
 *
 * @param game_state session to validate against
 * @param pawn_idx 0-based index of the pawn the player wishes to knock down
 * @return @c true if the move is legal, @c false otherwise.
 *
 * @warning This function does not check whose turn it is - turn validation is the responsibility of the message handler
 * layer.
**/
bool is_legal_move(GameState& game_state, uint32_t pawn_idx);

/**
 * @brief Returns whether at least one pawn is still standing on the board.
 *
 * @param game session to inspect.
 * @return @c true if at least one pawn is still standing, @c false otherwise.
**/
bool any_pawn_left(GameState& game);

/**
 * @brief Computes and returns the new game status after a move has been made.
 *
 * Call this immediately after knock_pawn_down() to determine whether the game continues or has ended and to advance
 * the turn.
 *
 * Decision logic:
 * - if no pawns remain: the player who has just made a move wins.
 * - if pawns remain: the turn is switched to the other player.
 *
 * @param game session to evaluate
 * @return one of: TURN_A, TURN_B, WIN_A, WIN_B depending on the game state after the move.
 *
 * @note The caller is responsible for writing the returned value back into the game.status and notifying the clients.
**/
uint8_t verify_game_state_after_move(GameState& game);

#endif //KAYLES_GAME_LOGIC_H
