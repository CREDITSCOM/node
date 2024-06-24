#ifndef ROUNDSTAT_SERIALIZER_HPP
#define ROUNDSTAT_SERIALIZER_HPP
#include <filesystem>

#include <boost/serialization/serialization.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/array.hpp>
#include <boost/serialization/split_member.hpp>

#include <lib/system/common.hpp>


namespace cs {
    class RoundStat;
    class RoundStat_Serializer {
    public:
        void bind(RoundStat& roundStat);
        void save(const std::filesystem::path& rootDir);
        void load(const std::filesystem::path& rootDir);
        void clear(const std::filesystem::path& rootDir);
        void printClassInfo();
        ::cscrypto::Hash hash();

    private:
        RoundStat* roundStat_;
    };
}

#endif 