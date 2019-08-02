#include <lmdb++.h>

namespace cs {
class LmdbException : public lmdb::error {
public:
    explicit LmdbException(const lmdb::error& error): lmdb::error(error) {}
};
}
