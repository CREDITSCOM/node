#ifndef NETWORKCOMMANDS_HPP
#define NETWORKCOMMANDS_HPP

#include <cinttypes>

enum class NetworkCommand : uint8_t {
    Registration = 2,
    RegistrationConfirmed,
    RegistrationRefused,
    Ping,
    BlockSyncRequest
};

enum class RegistrationRefuseReasons : uint8_t {
    Unspecified,
    LimitReached,
    BadId,
    BadClientVersion,
    Timeout,
    BadResponse,
    IncompatibleBlockchain
};

const char* networkCommandToString(NetworkCommand command);

#endif // NETWORK_COMMANDS_HPP
