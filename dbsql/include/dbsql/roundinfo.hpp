#ifndef ROUNDINFO_H
#define ROUNDINFO_H

#include <vector>
#include <string>

#include <lib/system/common.hpp>

namespace dbsql {
void saveConfidants(uint64_t round, const std::vector<cs::PublicKey>& confidants, uint64_t mask);
}

#endif // ROUNDINFO_H
