#pragma once

#include <string>

namespace client {
    struct Version {
        static const std::string GIT_SHA1;
        static const std::string GIT_DATE;
        static const std::string GIT_COMMIT_SUBJECT;
    };
}  // namespace clinet
