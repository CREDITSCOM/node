#pragma once

struct cmdline {
    static constexpr const char* argHelp     = "help";
    static constexpr const char* argVersion  = "version";
    static constexpr const char* argDBPath   = "db-path";
    static constexpr const char* argSeed     = "seed";
    static constexpr const char* argDumpKeys = "dumpkeys";
    static constexpr const char* argSetBCTop = "set-bc-top";
#ifdef _WIN32
    static constexpr const char* argInstall   = "install";
    static constexpr const char* argUninstall = "uninstall";
    static constexpr const char* argWorkDir   = "working_dir";
#endif
};