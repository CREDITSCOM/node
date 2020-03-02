#include <networkcommands.hpp>

const char* networkCommandToString(NetworkCommand command) {
    switch (command) {
    case NetworkCommand::Error:
        return "Error";
    case NetworkCommand::VersionRequest:
        return "VersionRequest";
    case NetworkCommand::VersionReply:
        return "VersionReply";
    case NetworkCommand::Ping:
        return "Ping";
    case NetworkCommand::Pong:
        return "Pong";
    default:
        return "Unknown";
    }
}
