/**
 * @file client_messages.cpp
 * @brief Implementation of client-side message serialization for the Kayles game.
**/

#include "client_messages.h"
#include "common.h"
#include "err.h"

#include <functional>
#include <unordered_map>
#include <stdexcept>

// expected payload sizes
constexpr size_t JOIN_SIZE = 5u;
constexpr size_t MOVE_SIZE = 10u;
constexpr size_t KEEP_ALIVE_SIZE = 9u;
constexpr size_t GIVE_UP_SIZE = 9u;

// message type bounds
constexpr int MIN_MESSAGE_TYPE = 0;
constexpr int MAX_MESSAGE_TYPE = 4;

constexpr char DELIMITER = '/';

constexpr int JOIN_PARTS = 2;
constexpr int MOVE_PARTS = 4;
constexpr int KEEP_ALIVE_PARTS = 3;
constexpr int GIVE_UP_PARTS = 3;
constexpr int PLAYER_PART = 1;
constexpr int GAME_PART = 2;
constexpr int PAWN_PART = 3;


// Type alias for the message serializer function
using SerializerFunction = std::function<void(
    const std::vector<std::string>&,
    std::vector<char>&
)>;

/**
 * @brief Extracts a field from the string tokens and converts it to uint32_t.
 *
 * @param parts the tokenized message
 * @param idx the index of the field to extract
 * @param field_name the logical name of the field (used for error reporting)
 * @return the parsed 32-bit unsigned integer
**/
static uint32_t extract_u32_field(const std::vector<std::string>& parts, size_t idx, const char* field_name) {
    // ensure the requested index actually exists in the provided tokens
    if (idx >= parts.size()) {
        fatal("missing field '%s' in message", field_name);
    }

    try {
        return static_cast<uint32_t>(stoul(parts[idx]));
    } catch (...) {
        fatal("invalid or non-numeric value for field '%s'", field_name);
    }
}

static void handle_join_game_client(const std::vector<std::string>&p, std::vector<char>& buf) {
    if (p.size() != JOIN_PARTS) {
        fatal("MSG_JOIN expects: 0/<player_id>");
    }

    buf.resize(JOIN_SIZE);
    size_t off = 1; // starting after the message type byte

    write_u32(buf, off, extract_u32_field(p, PLAYER_PART, "player_id"));
}

static void handle_make_move_one_client(const std::vector<std::string>&p, std::vector<char>& buf) {
    if (p.size() != MOVE_PARTS) {
        fatal("MSG_MOVE_1 expects: 1/<player_id>/<game_id>/<pawn_idx>");
    }

    buf.resize(MOVE_SIZE);
    size_t off = 1;

    write_u32(buf, off, extract_u32_field(p, PLAYER_PART, "player_id"));
    write_u32(buf, off, extract_u32_field(p, GAME_PART, "game_id"));
    buf[off] = static_cast<char>(extract_u32_field(p, PAWN_PART, "pawn_idx"));
}

static void handle_make_move_two_client(const std::vector<std::string>&p, std::vector<char>& buf) {
    if (p.size() != MOVE_PARTS) {
        fatal("MSG_MOVE_2 expects: 2/<player_id>/<game_id>/<pawn_idx>");
    }

    buf.resize(MOVE_SIZE);
    size_t off = 1;

    write_u32(buf, off, extract_u32_field(p, PLAYER_PART, "player_id"));
    write_u32(buf, off, extract_u32_field(p, GAME_PART, "game_id"));
    buf[off] = static_cast<char>(extract_u32_field(p, PAWN_PART, "pawn_idx"));
}

static void handle_keep_alive_client(const std::vector<std::string>&p, std::vector<char>& buf) {
    if (p.size() != KEEP_ALIVE_PARTS) {
        fatal("MSG_KEEP_ALIVE expects: 3/<player_id>/<game_id>");
    }

    buf.resize(KEEP_ALIVE_SIZE);
    size_t off = 1;

    write_u32(buf, off, extract_u32_field(p, PLAYER_PART, "player_id"));
    write_u32(buf, off, extract_u32_field(p, GAME_PART, "game_id"));
}

static void handle_give_up_client(const std::vector<std::string>&p, std::vector<char>& buf) {
    if (p.size() != 3) {
        fatal("MSG_GIVE_UP expects: 4/<player_id>/<game_id>");
    }

    buf.resize(GIVE_UP_SIZE);
    size_t off = 1;

    write_u32(buf, off, extract_u32_field(p, PLAYER_PART, "player_id"));
    write_u32(buf, off, extract_u32_field(p, GAME_PART, "game_id"));
}

/**
 * @brief Helper function to parse the command-line message string and serialize it into a binary UDP payload.
 *
 * The function reads the type of the message
 *
 * @param human_readable the message string
 * @return a vector of bytes representing the network payload
**/
std::vector<char> serialize_message(const std::string& human_readable) {
    // split the message accordingly
    const std::vector<std::string> parts = split_message(human_readable, DELIMITER);

    // check the length
    if (parts.empty()) {
        fatal("empty message");
    }

    int type_raw;

    try {
        type_raw = stoi(parts[0]);
    }
    catch (const std::exception& e) {
        fatal("invalid message type");
    }

    if (type_raw < MIN_MESSAGE_TYPE || type_raw > MAX_MESSAGE_TYPE) {
        fatal("invalid message type");
    }

    uint8_t type = static_cast<uint8_t>(type_raw);

    // add the message type to the buffer
    std::vector<char> buffer;
    buffer.push_back(static_cast<char>(type));

    static const std::unordered_map<uint8_t, SerializerFunction> serializers = {
        {0, handle_join_game_client},
        {1, handle_make_move_one_client},
        {2, handle_make_move_two_client},
        {3, handle_keep_alive_client},
        {4, handle_give_up_client}
    };

    auto it = serializers.find(type);

    // if matching serializer is found
    if (it != serializers.end()) {
        it->second(parts, buffer);
    } else {
        fatal("unknown message type");
    }

    return buffer;
}