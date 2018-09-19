#include <fstream>
#include <csnode/BlockHashes.h>
using namespace std;

namespace Credits
{
BlockHashes::BlockHashes(std::string dbs_fname)
    : dbs_fname_(dbs_fname)
    , db_{}
    , isDbInited_(false)
{
}

bool BlockHashes::loadDbStructure()
{
    std::ifstream f(dbs_fname_);
    if (!f.is_open())
        return false;

    std::cout << "File is opened ... reading" << std::endl;

    char  kk[14];
    f.read(kk, 14);
    f.close();

    char* s_beg = kk;
    char* s_end = strchr(kk, '-');
    *s_end = '\0';
    db_.first_ = atoi(s_beg);
    s_beg = s_end + 2;
    db_.last_ = atoi(s_beg);
    isDbInited_ = true;
    std::cout << "DB structure: " << db_.first_ << "->" << db_.last_ << std::endl;
    return true;
}

void BlockHashes::initStart()
{
}

bool BlockHashes::initFromPrevBlock(csdb::Pool prevBlock)
{
    csdb::Pool::sequence_t seq = prevBlock.sequence();
    if (!isDbInited_)
    {
        db_.first_ = 0;
        db_.last_ = seq;
        hashes_.reserve(db_.last_ + 1);
        isDbInited_ = true;
    }
    else if (seq > db_.last_)
        return false;

    size_t tmpInd = db_.last_ - seq;
    if (tmpInd != hashes_.size())
        return false;// see BlockChain::putBlock

    hashes_.emplace_back(prevBlock.hash());
    return true;
}

void BlockHashes::initFinish()
{
    if (hashes_.size() >= 2)
    {
        size_t lh = 0;
        size_t rh = hashes_.size() - 1;
        while (lh < rh)
            swap(hashes_[lh++], hashes_[rh--]);
    }

    for (const auto& hash : hashes_)
    {
        std::cout << "READ> " << hash.to_string() << std::endl;
    }
}

bool BlockHashes::loadNextBlock(csdb::Pool nextBlock)
{
    csdb::Pool::sequence_t seq = nextBlock.sequence();
    if (!isDbInited_)
    {
        db_.first_ = 0;
        db_.last_ = seq;
        isDbInited_ = true;
    }
    else if (seq <= db_.last_)
        return false;

    if (seq != hashes_.size())
        return false;// see BlockChain::putBlock

    hashes_.emplace_back(nextBlock.hash());
    db_.last_ = seq;
    return true;
}

bool BlockHashes::find(csdb::Pool::sequence_t seq, csdb::PoolHash& res) const
{
    if (empty())
        return false;
    const auto& range = getDbStructure();
    if (seq < range.first_ || range.last_ < seq)
        return false;
    res = hashes_[seq];
    return true;
}

bool BlockHashes::saveDbStructure()
{
    if (!isDbInited_)
        return false;
    std::ofstream f(dbs_fname_);
    if (!f.is_open())
        return false;
    f << db_.first_ << "->" << db_.last_ << std::endl;
    std::cout << "DB structure: " << db_.first_ << "->" << db_.last_ << std::endl;
    std::cout << "DB structure is written succesfully" << std::endl;
    return true;
}
}