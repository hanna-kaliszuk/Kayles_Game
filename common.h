/**
 * @file common.h
 * @brief Shared constants, data structures and utility functions for both server and client.
**/

#ifndef KAYLES_COMMON_H
#define KAYLES_COMMON_H

#include <string>
#include <vector>
#include <cstdint>

// returned by validation functions to signal an invalid input
constexpr int INVALID_VALUE = (-1);

// maximum size of a single UDP receive buffer in bytes
constexpr int BUFFER_SIZE = 1000;

// expected byte length of a WRONG_MESSAGE server response
constexpr int WRONG_MSG_LEN = 14;

// status code in WRONG_MESSAGE response
constexpr int ERROR_STATUS = 255;

// byte offset withing a WRONG_MESSAGE response
constexpr int MESSAGE_LEN = 12;

// returned by validate_message_format() when no error is found
constexpr int NO_ERROR = (-1);

/**
 * @brief Runtime configuration of the application, parsed from command line arguments.
 *
 * Fields are shared between the server and client executables; which fields
 * are required depends on the allowed_flags passed to parse_arguments().
**/
struct AppConfig {
    std::string address;         // IPv4 address (dotted-decimal or domain)
    std::string pawn_row;        // initial board layout
    std::string message;         // human-readable message to send
    int port = INVALID_VALUE;    // UDP port number
    double timeout = static_cast<double>(INVALID_VALUE); // session inactivity timeout in seconds
};

/**
 * @brief Parses a decimal integer string and validates it against and inclusive range.
 *
 * Uses strtol() for conversion. Returns INVALID_VALUE if the string is not a valid number or if the number falls
 * outside the given range.
 *
 * @param text_value null-terminated string to parse
 * @param min_value minimum acceptable value (inclusive)
 * @param max_value maximum acceptable value (inclusive)
 * @return parsed integer on success, INVALID_VALUE on failure
**/
int validate_and_convert_number(const char* text_value, int min_value, int max_value);

/**
 * @brief Parses a double string and validates it against and inclusive range.
 *
 * Uses strtol() for conversion. Returns INVALID_VALUE if the string is not a valid number or if the number falls
 * outside the given range.
 *
 * @param text_value null-terminated string to parse
 * @param min_value minimum acceptable value (inclusive)
 * @param max_value maximum acceptable value (inclusive)
 * @return parsed double on success, INVALID_VALUE on failure
**/
double validate_and_convert_double(const char* text_value, double min_value, double max_value);

uint64_t get_current_time_ms();

/**
 * @brief Terminates the process if a flag has already been set.
 *
 * @param flag @c true if the flag has already been seen.
 * @param message error message forwarded to fatal() if @p flag is true
**/
void ensure_not_set(bool flag, const std::string& message);

/**
 * @brief Parses and validates all command-line arguments, populating @p config.
 *
 * Uses getopt() internally. Only flags that are listed in @p allowed_flags are accepted. All others cause a fatal error.
 * After option parsing, every flag present in @p allowed_flags is checked for presence - missing required flags are
 * fatal.
 *
 * Accepted flags:
 *  -a <address>   Server IP address.
 *  -p <port>      Port number (valid range differs between server and client).
 *  -t <timeout>   Inactivity timeout in seconds [1, 99].
 *  -r <pawn_row>  Initial board string (server only); must be non-empty,
 *                 contain only '0'/'1', start and end with '1', max 256 chars.
 *  -m <message>   Message string to send (client only).
 *
 * @param argc argument count from main()
 * @param argv argument vector from main()
 * @param config AppConfig struct to populate
 * @param allowed_flags getopt-style option string
 * @param is_server controls the valid port range:
 *                   - if @c true, valid port range is [0, 65535] (0 means auto-assign)
 *                   - if @c false, valid port range is [1, 65535]
**/
void parse_arguments(int argc, char* argv[], AppConfig& config, const char* allowed_flags, bool is_server);

/**
 * @brief Splits a string by a single-character delimiter into a vector of tokens.
 *
 * Consecutive delimiters produce empty tokens. The delimiter character itself
 * is not included in any token.
 *
 * @param message input string to split
 * @param delimiter character to split on
 * @return ordered vector of tokens
**/
std::vector<std::string> split_message(const std::string& message, char delimiter);

/**
 * @brief Validates the structure of a split protocol message.
 *
 * Checks if:
 * - the number of parts matches @p expected_parts_count
 * - all fields after the first consist solely of digits
 *
 * Return semantics:
 * - NO_ERROR (-1) if the message is well-formed
 * - 0...N - the byte offset of the first detected error within the original buffer
 *
 * @param buffer original raw message string
 * @param parts result of split_message() on a @p buffer
 * @param expected_parts_count number of delimited tokens the message type requires
 * @return NO_ERROR if valid, or the error byte offset otherwise.
**/
int validate_message_format(const std::string& buffer, const std::vector<std::string>& parts,
                            size_t expected_parts_count);

/**
 * @brief Serialises a uint32_t value into a byte vector in network byte order (big-endian).
 *
 * Writes exactly 4 bytes starting at @p off and advances @p off by 4.
 *
 * @param out  Destination byte vector (must have sufficient capacity from @p off onward).
 * @param off  Write offset, updated in place after the write.
 * @param val  Host-byte-order value to serialize.
**/
void write_u32(std::vector<char>& out, size_t& off, uint32_t val);

/**
 * @brief Deserialises a uint32_t from a raw byte buffer in network byte order (big-endian).
 *
 * Reads exactly 4 bytes starting at @p buf + @p offset and converts them to
 * host byte order via ntohl().
 *
 * @param buf     Source buffer (must contain at least @p offset + 4 bytes).
 * @param offset  Byte offset into @p buf from which to read.
 * @return        Deserialised value in host byte order.
**/
uint32_t read_u32(const char* buf, size_t offset);

#endif //KAYLES_COMMON_H
