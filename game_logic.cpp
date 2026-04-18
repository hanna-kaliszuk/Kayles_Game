/**
 * @file game_logic.cpp
 * @brief Implementation of Kayles board operations and game-state transitions.
 *
 * ##### Bit-Addressing #####
 * Pawns are stored MSB-first within each byte so that the string representation of the board matches the natural
 * left-to-right reading order of the binary string.
**/

#include "game_logic.h"

using namespace std;

// number of pawns packed into a single byte of pawn_row
constexpr size_t BITS_PER_BYTE = 8u;

// offset used to convert a within-byte pawn position into a bit index.
constexpr size_t BIT_INDEX_OFFSET = BITS_PER_BYTE - 1u;

void initialize_pawn_row(const string& str_pawns, GameState& game) {
    // indices are 0-based
    game.max_pawn = static_cast<uint8_t>(str_pawns.length() - 1);

    // allocate the minimum number of bytes needed to hold all pawns
    const size_t num_bytes = (game.max_pawn / BITS_PER_BYTE) + 1;

    // clear all bits
    game.pawn_row.assign(num_bytes, 0);

    for (size_t i = 0; i < str_pawns.length(); i++) {
        if (str_pawns[i] == PAWN_STANDING) {
            const size_t byte_index = i / BITS_PER_BYTE;
            const size_t bit_index = BIT_INDEX_OFFSET - (i % BITS_PER_BYTE);

            game.pawn_row[byte_index] |= (1 << bit_index);
        }
    }
}

string serialize_pawn_row(const GameState& game) {
    string result;

    // reconstruct the string
    for (size_t i = 0; i <= game.max_pawn; i++) {
        const size_t byte_index = i / BITS_PER_BYTE;
        const size_t bit_index = BIT_INDEX_OFFSET - (i % BITS_PER_BYTE);

        // check if the pawn is standing
        const bool is_set = (game.pawn_row[byte_index] & (1 << bit_index)) != 0;

        result += (is_set ? PAWN_STANDING : PAWN_KNOCKED_DOWN);
    }
    return result;
}

void knock_pawn_down(GameState& game_state, uint32_t pawn_idx) {
    const size_t byte_idx = pawn_idx / BITS_PER_BYTE;
    const size_t bit_idx = BIT_INDEX_OFFSET - (pawn_idx % BITS_PER_BYTE);

    // clear pawn's bit
    game_state.pawn_row[byte_idx] &= ~(1 << bit_idx);
}

bool is_pawn_standing(GameState& game_state, const uint32_t pawn_idx) {
    const size_t byte_idx = pawn_idx / BITS_PER_BYTE;
    const size_t bit_idx = BIT_INDEX_OFFSET - (pawn_idx % BITS_PER_BYTE);

    // check if the bit is set
    if ((game_state.pawn_row[byte_idx] & (1 << bit_idx)) == 0) return false;
    return true;
}

bool is_legal_move(GameState& game_state, const uint32_t pawn_idx) {
    if (pawn_idx > game_state.max_pawn) return false;

    if (!is_pawn_standing(game_state, pawn_idx)) return false;

    return true;
}

bool any_pawn_left(GameState& game) {
    const uint8_t max_pawn = game.max_pawn;

    for (uint8_t i = 0; i <= max_pawn; i++) {
        if (is_pawn_standing(game, i)) {
            return true;
        }
    }

    return false;
}

uint8_t verify_game_state_after_move(GameState& game) {
    bool pawns_left = any_pawn_left(game);

    if (!pawns_left) {
        if (game.status == TURN_A) return WIN_A;
        return WIN_B;
    }

    if (game.status == TURN_A) return TURN_B;
    return TURN_A;
}
