#include <vector>
#include <csdb/pool.h> 

namespace cs
{
class BlockHashes
{
public:
    struct DbStructure
    {
        csdb::Pool::sequence_t first_;
        csdb::Pool::sequence_t last_;
    };
public:
    BlockHashes(std::string dbs_fname);
    bool loadDbStructure();
    const DbStructure& getDbStructure() const { return db_; }
    bool empty() const { return hashes_.empty(); }
    void initStart();
    bool initFromPrevBlock(csdb::Pool prevBlock);
    void initFinish();
    bool loadNextBlock(csdb::Pool nextBlock);
    bool saveDbStructure();

    bool find(csdb::Pool::sequence_t seq, csdb::PoolHash& res) const;
private:
    const std::string dbs_fname_;
    std::vector<csdb::PoolHash> hashes_;

    DbStructure db_;
    bool isDbInited_;
};
}
