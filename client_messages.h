/**
 * @file client_messages.h
 * @brief Client-side message handling utilities.
**/

#ifndef KAYLES_CLIENT_MESSAGES_H
#define KAYLES_CLIENT_MESSAGES_H

#include <vector>
#include <string>

/**
 * @brief Parses a command-line message string and serializes it into a binary UDP payload.
 *
 * Splits the string by the defined delimiter, checks the message type and handles it accordingly, verifying the
 * requirements for each type.
 *
 * @param human_readable the message string, expected to be in the format specified in kayles_client.cpp
 * @return a vector of bytes representing the network payload to be sent.
 *
**/
std::vector<char> serialize_message(const std::string& human_readable);

#endif //KAYLES_CLIENT_MESSAGES_H