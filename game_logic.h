#ifndef KAYLES_GAME_LOGIC_H
#define KAYLES_GAME_LOGIC_H

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <netinet/in.h>

using namespace std;

constexpr uint8_t WAITING_FOR_OPPONENT = 0;
constexpr uint8_t TURN_A = 1;
constexpr uint8_t TURN_B = 2;

struct GameState {
    uint32_t player_a_id;
    uint32_t player_b_id;
    uint8_t status;
    uint8_t max_pawn;
    vector<uint8_t> pawn_row;
};

using MessageHandler = function<void(
    const vector<string>& parts,
    unordered_map<uint32_t, GameState>& active_games,
    const GameState& template_game,
    int socket_fd,
    const struct sockaddr_in& client_addr
)>;

void initialize_pawn_row(const string& str_pawns, GameState& game);

string serialize_pawn_row(const GameState& game);

#endif //KAYLES_GAME_LOGIC_H