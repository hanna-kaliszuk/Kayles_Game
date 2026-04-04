#include <unistd.h>
#include <iostream>
#include <string>

#include "common.h"

using namespace std;

#define MAX_PORT_NUMBER 65535
#define MAX_TIMEOUT_VALUE 99
#define MIN_VALUE 1

struct ClientConfig {
    string address;
    int port;
    int timeout;
    string message;
};

static void parse_client_arguments(int argc, char* argv[], ClientConfig& config) {
    bool has_port = false;
    bool has_address = false;
    bool has_timeout = false;
    bool has_message = false;

    int opt;

    while ((opt = getopt(argc, argv, "a:p:m:t:")) != -1) {
        switch (opt) {
        case 'a':
            if (has_address) {
                cerr << "error: multiple -a options provided." << endl;
                exit(EXIT_FAILURE);
            }

            config.address = optarg;
            has_address = true;
            break;

        case 'p':
            if (has_port) {
                cerr << "error: multiple -p options provided." << endl;
                exit(EXIT_FAILURE);
            }

            config.port = validate_and_convert_number(optarg, MIN_VALUE, MAX_PORT_NUMBER);

            if (config.port == -1) {
                cerr << "error: invalid port number. expected value from 1 to 2^16 - 1." << endl;
                exit(EXIT_FAILURE);
            }

            has_port = true;
            break;

        case 'm':
            if (has_message) {
                cerr << "error: multiple -m options provided." << endl;
                exit(EXIT_FAILURE);
            }

            config.message = optarg;
            has_message = true;
            break;

        case 't':
            if (has_timeout) {
                cerr << "error: multiple -t options provided." << endl;
                exit(EXIT_FAILURE);
            }

            config.timeout = validate_and_convert_number(optarg, MIN_VALUE, MAX_TIMEOUT_VALUE);
            if (config.timeout == -1) {
                cerr << "error: invalid timeout value. expected a value from 1 to 99." << endl;
                exit(EXIT_FAILURE);
            }

            has_timeout = true;
            break;

        case '?':
        default:
            cerr << "error: unknown option or missing argument. expected: -a, -p, -m, -t."<< endl;
            exit(EXIT_FAILURE);
        }
    }

    if (!has_address || !has_port || !has_message || !has_timeout) {
        cerr << "error: missing required arguments." << endl;
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char* argv[]) {
    ClientConfig config;

    parse_client_arguments(argc, argv, config);

    cout << "--- SUCCESS: arguments parsed correctly ---" << endl;
    cout << "address:       " << config.address << endl;
    cout << "port:          " << config.port << endl;
    cout << "message:       " << config.message << endl;
    cout << "timeout:       " << config.timeout << endl;
}