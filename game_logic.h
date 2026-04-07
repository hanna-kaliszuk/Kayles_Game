#ifndef KAYLES_GAME_LOGIC_H
#define KAYLES_GAME_LOGIC_H

#include <string>
#include <vector>
#include <netinet/in.h>

constexpr uint8_t WAITING_FOR_OPPONENT = 0;
constexpr uint8_t TURN_A = 1;
constexpr uint8_t TURN_B = 2;
constexpr uint8_t WIN_A = 3;
constexpr uint8_t WIN_B = 4;

struct GameState {
    uint32_t player_a_id;
    uint32_t player_b_id;
    uint8_t status;
    uint8_t max_pawn;
    std::vector<uint8_t> pawn_row;
};

void initialize_pawn_row(const std::string& str_pawns, GameState& game);

std::string serialize_pawn_row(const GameState& game);

void knock_pawn_down(GameState& game_state, uint32_t pawn_idx);

bool is_pawn_standing(GameState& game_state, const uint32_t pawn_idx);

bool is_legal_move(GameState& game_state, const uint32_t pawn_idx);

#endif //KAYLES_GAME_LOGIC_H