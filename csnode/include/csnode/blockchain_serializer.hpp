#ifndef BLOCKCHAIN_SERIALIZER_HPP
#define BLOCKCHAIN_SERIALIZER_HPP
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/map.hpp>

#include <lib/system/common.hpp>

class BlockChain;

namespace cs {
class BlockChain_Serializer {
public:
    void bind(BlockChain&);
    void save();
    void load();
    void clear();

private:
    struct NonEmptyBlockData {
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
            ar & poolSeq;
            ar & transCount;
        }
        cs::Sequence poolSeq;
        uint32_t transCount;
    };

    std::map<cs::Sequence, NonEmptyBlockData> *previousNonEmpty_;
    NonEmptyBlockData *lastNonEmptyBlock_;

    uint64_t *totalTransactionsCount_;
};
}  // namespace cs
#endif //  BLOCKCHAIN_SERIALIZER_HPP
