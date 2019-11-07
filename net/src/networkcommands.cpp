#include <networkcommands.hpp>

const char* networkCommandToString(NetworkCommand command) {
    switch (command) {
    case NetworkCommand::Registration:
        return "Registration";
    case NetworkCommand::RegistrationConfirmed:
        return "RegistrationConfirmed";
    case NetworkCommand::RegistrationRefused:
        return "RegistrationRefused";
    case NetworkCommand::Ping:
        return "Ping";
    case NetworkCommand::BlockSyncRequest:
        return "BlockSyncRequest";
    default:
        return "Unknown";
    }
}
