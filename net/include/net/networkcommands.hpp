#ifndef NETWORKCOMMANDS_HPP
#define NETWORKCOMMANDS_HPP

#include <cinttypes>

enum class NetworkCommand : uint8_t {
    Error = 1,
    VersionRequest,
    VersionReply,
    Ping,
    Pong
};

const char* networkCommandToString(NetworkCommand command);

#endif // NETWORK_COMMANDS_HPP
