#include "game_logic.h"

using namespace std;

void initialize_pawn_row(const string& str_pawns, GameState& game) {
    game.max_pawn = static_cast<uint8_t>(str_pawns.length() - 1);

    const size_t num_bytes = (game.max_pawn / 8) + 1;

    game.pawn_row.assign(num_bytes, 0);
    for (size_t i = 0; i < str_pawns.length(); i++) {
        if (str_pawns[i] == '1') {
            size_t byte_index = i /8;
            size_t bit_index = 7 - (i % 8);

            game.pawn_row[byte_index] |= (1 << bit_index);
        }
    }
}

string serialize_pawn_row(const GameState& game) {
    string result = "";

    for (int i = 0; i <= game.max_pawn; i++) {
        size_t byte_index = i / 8;
        size_t bit_index = 7 - (i % 8);
        bool is_set = (game.pawn_row[byte_index] & (1 << bit_index)) != 0;
        result += (is_set ? "1" : "0");
    }
    return result;
}