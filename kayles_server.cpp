/**
 * @file kayles_server.cpp 
 * @brief Implementation of the UDP server for the network Kayles game. 
 * 
 * 1. GAME RULES: 
 * Kayles is a simple, two-player game played on a row of N pawns. 
 * 
 * ##### Turn mechanics #####
 * - Players alternate turns (player A = 1, player B = 2)
 * - On each turn the active player must perform exactly one of: 
 *      a) knock down a single standing pawn (MAKE_MOVE_1)
 *      b) knock down two adjacent standing pawns (MAKE_MOVE_2)
 * - A player who cannot perform a legal move loses the game
 * 
 * ##### Game And Conditions #####
 * - Normal end :    no legal move available for the active player
 * - Forfeit    :    one of the players sens GIVE_UP
 * - Timeout    :    a session receives no messages for <timeout> seconds
 * 
 * 2. APLICATION ARCHITECTURE:
 * - The server is single-threaded and handles all game in one event loop driven by recvfrom()
 * - Games are identified by a 32-bit game_id (1, ..., 2^32 - 1)
 * - A single GameState stores the complete session: board bitmask, player IDs, whose turn it is and a last_activity
 *   timestamp used for eviction
 * - Stale sessions are evicted either on every received packet or when SO_RCVTIMEO fires 
 * 
 * 3. PROTOCOL SPECIFICATION: 
 * All integers are in network byte order. 
 * 
 * 4. GAME-STATE FIELDS
 * - game_id    :   32-bites, non-negative integer      : unique session identifier 
 * - players    :   32-bites, non-negative integers     : unique player identifiers
 * - status     :   8-bites, non-negative integer       : current session status
 *                                                        - WAITING_FOR_OPPONENT (0)    - player A connected, waiting
 *                                                                                        for player B
 *                                                        - TURN_A (1) / TURN_B (2)     - waiting for player A / B to
 *                                                                                        make a move
 *                                                        - WIN_A (3) / WIN_B (4)       - player A / B won the game 
 * - max_pawn   :   8-bites, non-negative integer       : the max index of the pawn 
 * - pawn_row   :   floor(max_pawn / 8) byte array      : a bitmask representing a row of pawns
 *                                                        - 1 - a pawn is present on the field 
 *                                                        - 0 - the field is empty or the pawn has been captured 
 * 
 * 5. CLIENT -> SERVER MESSAGES:
 * Client -> server messages consist of:
 * [msg_type = 1 byte][player_id = 4 bytes][game_id = 4 bytes][pawn_idx = 1 bytes]
 * 
 * Client might send 5 kinds of messages:
 * - MSG_JOIN           :   [msg_type = 0][player_id]                   : player <player_id> wants to join a game
 * - MSG_MOVE_1         :   [msg_type = 1][player_id][game_id][pawn]    : player <player_id> wants to knock down a
 *                                                                        <pawn> pawn in game <game_id>
 * - MSG_MOVE_2         :   [msg_type = 2][player_id][game_id][pawn]    : player <player_id> wants to knock down a
 *                                                                        <pawn> pawn and <pawn + 1> pawns in game
 *                                                                        <game_id>
 * - MSG_KEEP_ALIVE     :   [msg_type = 3][player_id][game_id]          : player <player_id> pings <game_id> game to
 *                                                                        avoid timeout
 * - MSG_GIVE_UP        :   [msg_type = 4][player_id][game_id]          : player <player_id> forfeits <game_id> game
 * 
 * Messages with game_id included affect only the designated game. They do not affect the rest of the games the player
 * might be taking part in.
 * 
 * 6. SERVER -> CLIENT MESSAGES: 
 * In case the client's message was correct, the server responds with MSG_GAME_STATE with the structure described in 4.
 * An exception is made when a correct MSG_JOIN message has been sent but the server is unable to create a new session
 * due to, for example, memory allocation errors or unique game identifiers shortage.
 * 
 * For a message to be correct, the following requirements must be complied with: 
 * - it has the correct length, 
 * - player_id > 0, 
 * - game_id game exists and player_id is a registered player in the given session.
 * 
 * ##### Illegal Moves #####
 * In case an incorrect pawn index is provided the message is correct, however, the move is illegal. 
 * The move is also illegal if it cannot be made in the current game state or if it is not the current players turn.
 * The same goes for the MSG_GIVE_UP.
 * 
 * ##### Malformed Messages #####
 * In case a malformed message is provided by a client, the server responds with MSG_WRONG_MESSAGE:
 * - 12-bite echoing the maximum of the first 12 bites of the client message (unused bites = 0), 
 * - status         - 8-bites integer = 255, 
 * - error_index    - 8-bites integer informing of the bite index the server was unable to process. 
 * 
 * Those messages are sent back to the address and port number the original message was sent from. 
 * 
 * 7. RUNNING THE SERVER: 
 * All of the listed parameters are obligatory. However, they can be provided in an arbitrary order:
 * * -r pawn_row        :   a string defining the initial arrangement of pawns, consisting of a sequence of 0 and 1,
 *                          with no other symbols allowed. The minimum length is 1 and the maximum is 256.
 *                          The first fields always contains a pawn at position 0. The first and the last must be 1.
 * * -a address         :   a string representing the servers IP address or domain name in dotted notation 
 * * -p port            :   the port number on which the server listens. This is a base-10 integer in the range 0 to
 *                          2^16 - 1. A value of 0 means any available port.
 * * -t server_timeout  :   the timeout (in seconds) for waiting for the next client message. This is a base-10 integer
 *                          in the range 1 to 99.
 * 
**/

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>

#include "common.h"
#include "err.h"
#include "message_handlers.h"

constexpr uint8_t ERROR_IDX_TYPE = 0; 

/**
 * @brief Type alias for the message dispatch function to simplify the handler map. 
**/
using MessageHandler = std::function<void(
    const char* buf,
    size_t len,
    std::unordered_map<uint32_t, GameState>& active_games,
    const GameState& template_game,
    int socket_fd,
    const struct sockaddr_in& client_addr
)>;

/**
 * @brief Helper function to create and configure a UDP socket for the server. 
 * 
 * @param config The application configuration containing address, pawn_row, message, port and timeout.
 * 
 * @return The configured socket file descriptor. 
 * 
**/
static int create_sever_socket(const AppConfig& config) {
    // create a standard IPv4 UDP socket 
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_fd < 0) {
        syserr("unable to create socket for server socket.");
    }

    // set the timeout accordingly 
    struct timeval tv{};
    tv.tv_sec = static_cast<time_t>(config.timeout);
    tv.tv_usec = static_cast<suseconds_t>((config.timeout - tv.tv_sec) * 1000000.0);
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        syserr("setsockopt failed");
    }

    struct sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(config.port);

    struct addrinfo hints{}; 
    struct addrinfo* res = nullptr; 

    hints.ai_family = AF_INET;       
    hints.ai_socktype = SOCK_DGRAM;

    int err = getaddrinfo(config.address.c_str(), nullptr,&hints,&res);

    if (err != 0 || !res) {
        fatal("invalid IP address provided.");
    }

    // extract the binary IPv4 address
    server_address.sin_addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr;
    
    freeaddrinfo(res);

    // bind the socket to the local address and port.
    if (::bind(socket_fd, reinterpret_cast<struct sockaddr*>(&server_address),
               static_cast<socklen_t>(sizeof(server_address))) < 0) {
        syserr("unable to bind to port %d", config.port);
    }

    // get the actual port (in case 0 was provided)
    sockaddr_in addr{}; 
    socklen_t len = sizeof(addr); 

    if (getsockname(socket_fd, (sockaddr*)&addr, &len) == 0) {
        std::cout << "running server on port " << ntohs(addr.sin_port) << std::endl;
    }

    return socket_fd;
}

/**
 * @brief Helper function to remove timeouted games. 
 * 
 * Goes through all the active games and for each one verifies it the last connection to the session has been made
 * within <server_timeout> seconds. If not, the game is removed from the active sessions.
 * 
 * @param active_games the map of the active games
 * @param timeout_seconds server timeout
 * 
 * @note The function modifies active_games structure. 
**/
static void remove_timed_out_games(std::unordered_map<uint32_t, GameState>& active_games, const double timeout_seconds) {
    const uint64_t current_time = get_current_time_ms();

    // to miliseconds
    const uint64_t timeout_ms = static_cast<uint64_t>(timeout_seconds * 1000);

    // chceck for every active game
    for (auto it = active_games.begin(); it != active_games.end();) {
        if (current_time - it->second.last_activity > timeout_ms) {
            std::cout << "game no " << it->first << " timed out" << std::endl;
            it = active_games.erase(it);
        }
        else {
            ++it;
        }
    }
}

/**
 * @brief Helper function to decode the message and dispatch it to the right handler. 
 * 
 * The function identifies the type of the request by inspecting the first byte of the message. If the message type is
 * recognized, the corresponding logic is executed; otherwise MSG_WRONG_MSG message is sent to the requesting client.
 * 
 * @param buf pointer to the raw binary buffer received from the network
 * @param len size of the received buffer in bytes (must match JOIN_SIZE)
 * @param active_games the map of currently active games indexed by game ID
 * @param template_game a template representing a brand-new game state
 * @param socket_fd server socket file descriptor
 * @param client_addr the address structure of the client that sent the request
 * 
 **/
static void decode_and_verify_message(const char* buf, size_t len, std::unordered_map<uint32_t, GameState>& active_games,
    const GameState& template_game, int socket_fd, const struct sockaddr_in& client_addr) {
    // validate message length 
    if (len == 0) {
        handle_wrong_message(buf, len, ERROR_IDX_TYPE, socket_fd, client_addr);
        return;
    }

    const uint8_t msg_type = static_cast<uint8_t>(buf[0]);

    // static map of functions for O(1) dispatch 
    static const std::unordered_map<uint8_t, MessageHandler> handlers = {
        {0u, handle_join_game},
        {1u, handle_make_move_one},
        {2u, handle_make_move_two},
        {3u, handle_keep_alive},
        {4u, handle_give_up}
    };

    // execute logic corresponding to the request type 
    auto it = handlers.find(msg_type);
    if (it != handlers.end()) {
        // dispatch the buffer to the specific handler 
        it->second(buf, len, active_games, template_game, socket_fd, client_addr);
    }
    else {
        // if the type is not regonized, notify the client about the malformed message
        handle_wrong_message(buf, len, ERROR_IDX_TYPE, socket_fd, client_addr);
    }
}

/**
 * @brief Helper function to run the main server loop. 
 * 
 * Receives datagrams from clients, logs them, maintains active game sessions and dispatches messages for decoding,
 * validation and game state updates. Also removes timed-out game sessions.
 * 
 * @param config server configuration struct
 * @param template_game template used to initialize new game sessions 
**/
static void run_server(const AppConfig& config, const GameState& template_game) {
    // create UDP server socket 
    int socket_fd = create_sever_socket(config);

    // buffer for incoming messages 
    char buffer[BUFFER_SIZE];

    std::unordered_map<uint32_t, GameState> active_games;

    while (true) {
        struct sockaddr_in client_address;
        socklen_t client_address_length = sizeof(client_address);

        // receive next UDP packet 
        ssize_t received_length = recvfrom(socket_fd, buffer, BUFFER_SIZE - 1, 0,
                                           reinterpret_cast<struct sockaddr*>(&client_address), &client_address_length);

        // handle timeout
        if (received_length < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED) {
                // clean up timed-out games 
                remove_timed_out_games(active_games, config.timeout);
                continue;
            }
            else {
                syserr("recvfrom failed");
            }
        }

        const size_t received_len = static_cast<size_t>(received_length);

        // convert client IP to a readable one 
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_address.sin_addr, client_ip, sizeof(client_ip));

        // convert client port 
        uint16_t client_port = ntohs(client_address.sin_port);

        std::cout << "received " << received_len << " bytes from "
            << client_ip << ":" << client_port << std::endl;

        // clean up timed-out games 
        remove_timed_out_games(active_games, config.timeout);

        // handle incoming message 
        decode_and_verify_message(buffer, received_len, active_games, template_game, socket_fd, client_address);
    }
}

int main(int argc, char* argv[]) {
    AppConfig config;
    GameState template_game;

    parse_arguments(argc, argv, config, "r:a:p:t:", true);
    
    initialize_pawn_row(config.pawn_row, template_game);

    run_server(config, template_game);

    return 0;
}