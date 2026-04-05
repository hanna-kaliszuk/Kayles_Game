#include "common.h"
#include "err.h"

#define MAX_PORT_NUMBER 65535
#define MAX_TIMEOUT_VALUE 99
#define MIN_PORT_VALUE 1
#define MIN_TIMEOUT_VALUE 1
#define INVALID_VALUE (-1)

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
            ensure_not_set(has_address, "multiple -a options provided.");
            config.address = optarg;
            has_address = true;
            break;

        case 'p':
            ensure_not_set(has_port, "multiple -p options provided.");
            config.port = validate_and_convert_number(optarg, MIN_PORT_VALUE, MAX_PORT_NUMBER);

            if (config.port == INVALID_VALUE) {
                fatal("invalid port number. expected value from 1 to 2^16 - 1.");
            }

            has_port = true;
            break;

        case 'm':
            ensure_not_set(has_message, "multiple -m options provided.");
            config.message = optarg;
            has_message = true;
            break;

        case 't':
            ensure_not_set(has_timeout, "multiple -t options provided." );
            config.timeout = validate_and_convert_number(optarg, MIN_TIMEOUT_VALUE, MAX_TIMEOUT_VALUE);

            if (config.timeout == INVALID_VALUE) {
                fatal("invalid timeout value. expected a value from 1 to 99.");
            }

            has_timeout = true;
            break;

        case '?':
        default:
            fatal("unknown option or missing argument. expected: -a, -p, -m, -t");
        }
    }

    if (!has_address || !has_port || !has_message || !has_timeout) {
        fatal("missing required arguments. expected: -a, -p, -m, -t");
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