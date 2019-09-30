#include <csdb/storage.hpp>

#include <algorithm>
#include <cassert>
#include <condition_variable>
#include <cstdarg>
#include <deque>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

#include <csdb/address.hpp>
#include <csdb/database.hpp>
#include <csdb/database_berkeleydb.hpp>
#include <csdb/internal/shared_data_ptr_implementation.hpp>
#include <csdb/internal/utils.hpp>
#include <csdb/pool.hpp>
#include <csdb/wallet.hpp>

#include "binary_streams.hpp"

using namespace boost::multi_index;

namespace {
struct last_error_struct {
    ::csdb::Storage::Error last_error_ = ::csdb::Storage::NoError;
    std::string last_error_message_;
};

last_error_struct& last_error_map(const void* p) {
    static thread_local ::std::map<const void*, last_error_struct> last_errors_;
    return last_errors_[p];
}
}  // namespace

namespace csdb {

namespace {

struct head_info_t {
    size_t len_;     // Количество блоков в цепочке
    PoolHash next_;  // хеш следующего пула, или пустая строка для первого пула
                     // в цепочее (нет родителя, начало цепочки).
};
using heads_t = std::map<PoolHash, head_info_t>;
using tails_t = std::map<PoolHash, PoolHash>;

[[maybe_unused]] void update_heads_and_tails(heads_t& heads, tails_t& tails, const PoolHash& cur_hash, const PoolHash& prev_hash) {
    auto ith = heads.find(prev_hash);
    auto itt = tails.find(cur_hash);
    bool eith = (heads.end() != ith);
    bool eitt = (tails.end() != itt);
    if (eith && eitt) {
        // Склеиваем две подцепочки.
        assert(1 == heads.count(itt->second));
        head_info_t& ith1 = heads[itt->second];
        ith1.next_ = ith->second.next_;
        ith1.len_ += (1 + ith->second.len_);
        if (!ith->second.next_.is_empty()) {
            /// \todo Проверить, почему выпадает assert!
            // assert(1 == tails.count(ith->second.next_));
            tails[ith->second.next_] = itt->second;
        }
        heads.erase(ith);
        // Мы, возможно, уже изменили tails - поэтому нельзя удалять по итератору!
        tails.erase(cur_hash);
    }
    else if (eith && (!eitt)) {
        // Добавляем в начало цепочки.
        if (!ith->second.next_.is_empty()) {
            /// \todo Проверить, почему выпадает assert!
            // assert(1 == tails.count(ith->second.next_));
            tails[ith->second.next_] = cur_hash;
        }
        assert(0 == heads.count(cur_hash));
        heads.emplace(cur_hash, head_info_t{ith->second.len_ + 1, ith->second.next_});
        heads.erase(prev_hash);
    }
    else if ((!eith) && eitt) {
        // Добавляем в конец цепочки.
        assert(1 == heads.count(itt->second));
        head_info_t& ith1 = heads[itt->second];
        ith1.next_ = prev_hash;
        ++ith1.len_;
        if (!prev_hash.is_empty()) {
            // assert не нужен, т.е. наличие такого "хвоста" говорит о пересекающихся или зацикленных
            // цепочках (т.е. уже была цепочка, имеющая этот же хвост).
            // TODO: Доделать детектирование таких цепочек (после создания unit-тестов)
            // assert(0 == tails.count(prev_hash));
            tails.emplace(prev_hash, itt->second);
        }
        tails.erase(cur_hash);
    }
    else {
        // Ни с чем не пересекаемся! Просто подвешиваем.
        assert(0 == heads.count(cur_hash));
        heads.emplace(cur_hash, head_info_t{1, prev_hash});
        if (!prev_hash.is_empty()) {
            // см. TODO к пердыдущей ветке.
            // assert(0 == tails.count(prev_hash));
            tails.emplace(prev_hash, cur_hash);
        }
    }
}

}  // namespace

class Storage::priv {
public:
    priv() {
        last_error_map(this) = last_error_struct();
    }

    ~priv() {
        if (write_thread.joinable()) {
            quit = true;
            write_cond_var.notify_one();
            write_thread.join();
        }
    }

private:
    bool rescan(Storage::OpenCallback callback);
    void write_routine();

    std::shared_ptr<Database> db = nullptr;
    PoolHash last_hash;     // Хеш последнего пула
    size_t count_pool = 0;  // Количество пулов транзакций в хранилище (первоночально заполняется в check)

    void set_last_error(Storage::Error error = Storage::NoError, const ::std::string& message = ::std::string());
    void set_last_error(Storage::Error error, const char* message, ...);

    std::thread write_thread;
    bool quit = false;

    std::mutex data_lock;

    std::deque<Pool> write_queue;
    std::mutex write_lock;
    std::condition_variable write_cond_var;

    struct PoolElement {
        cs::Sequence seq; struct bySequence {};
        PoolHash hash;  struct byHash {};
        Pool pool;
    };

    typedef multi_index_container<
        PoolElement,
        indexed_by<
            hashed_unique<
                tag<PoolElement::bySequence>, member<
                    PoolElement, cs::Sequence, &PoolElement::seq
                >
            >,
            hashed_unique<
                tag<PoolElement::byHash>, member<
                    PoolElement, PoolHash, &PoolElement::hash
                >
            >
        >
    > PoolCache;
    PoolCache pools_cache;
    static const size_t cacheSize = 10000;

    void pools_cache_insert(const cs::Sequence& seq, const PoolHash &hash, const Pool &pool) {
        if (pools_cache.size() == cacheSize) {
            auto random = pools_cache.begin();
            pools_cache.erase(random);
        }
        pools_cache.insert({seq, hash, pool});
    }

    friend class ::csdb::Storage;

private signals:
    ReadBlockSignal read_block_event;
    BlockReadingStartedSingal start_reading_event;
};

void Storage::priv::set_last_error(Storage::Error error, const ::std::string& message) {
    last_error_struct& les = last_error_map(this);
    les.last_error_ = error;
    les.last_error_message_ = message;
}

void Storage::priv::set_last_error(Storage::Error error, const char* message, ...) {
    last_error_struct& les = last_error_map(this);
    les.last_error_ = error;

    if (nullptr != message) {
        va_list args1;
        va_start(args1, message);
        va_list args2;
        va_copy(args2, args1);
        les.last_error_message_.resize(std::vsnprintf(NULL, 0, message, args1) + 1);
        va_end(args1);
        std::vsnprintf(&(les.last_error_message_[0]), les.last_error_message_.size(), message, args2);
        va_end(args2);
        les.last_error_message_.resize(les.last_error_message_.size() - 1);
    }
    else {
        les.last_error_message_.clear();
    }

    if (error != Storage::Error::NoError) {
        if (!les.last_error_message_.empty()) {
            cserror() << "Storage> error #" << static_cast<int>(error) << ": " << les.last_error_message_;
        }
        else {
            cserror() << "Storage> error #" << static_cast<int>(error);
        }
    }
}

bool Storage::priv::rescan(Storage::OpenCallback callback) {
    last_hash = {};
    count_pool = 0;

    heads_t heads;
    tails_t tails;

    Database::IteratorPtr it = db->new_iterator();
    assert(it);

    it->seek_to_last();
    if (it->is_valid()) {
        cs::Bytes v = it->value();
        Pool p = Pool::from_binary(std::move(v));
        if (p.is_valid()) {
            emit start_reading_event(p.sequence());
        }
        else {
            emit start_reading_event(0);
        }
    }
    else {
        emit start_reading_event(0);
    }

    Storage::OpenProgress progress{0};
    for (it->seek_to_first(); it->is_valid(); it->next()) {
        cs::Bytes v = it->value();

        Pool p = Pool::from_binary(std::move(v));
        if (!p.is_valid()) {
            set_last_error(Storage::DataIntegrityError, "Data integrity error: Corrupted pool %d.", count_pool);
            cserror() << "Please restart node with command : client --set-bc-top " << count_pool - 1;
            return false;
        }
        pools_cache_insert(p.sequence(), p.hash(), p);

        bool test_failed = false;
        last_hash = p.hash();
        count_pool++;

        emit read_block_event(p, &test_failed);
        if (test_failed) {
            set_last_error(Storage::DataIntegrityError, "Data integrity error: client reported violation of logic in pool %d", p.sequence());
            return false;
        }

        //update_heads_and_tails(heads, tails, p.hash(), p.previous_hash());
        progress.poolsProcessed++;

        if (callback != nullptr) {
            if (callback(progress)) {
                set_last_error(Storage::UserCancelled);
                return false;
            }
        }
    }

    return true;

#if 0
    std::stringstream ss;
    ss << "More than one chains or orphan chains. List follows:" << std::endl;
    for (auto ith = heads.begin(); ith != heads.end(); ++ith) {
        ss << "  " << ith->first.to_string() << " (length = " << ith->second.len_ << "): ";
        if (ith->second.next_.is_empty()) {
            ss << "Normal";
        }
        else {
            ss << "Orphan";
        }
        ss << std::endl;
    }
    ss << std::ends;
    set_last_error(Storage::ChainError, ss.str());

    return false;
#endif
}

void Storage::priv::write_routine() {
    std::unique_lock<std::mutex> lock(write_lock);
    while (!quit) {
        write_cond_var.wait(lock);
        while (!write_queue.empty()) {
            Pool& pool = write_queue.front();
            if (!pool.is_read_only()) {
                if (!pool.compose()) {
                    set_last_error(Storage::DataIntegrityError, "Pool passed to storage is not composed and failed to compose now");
                }
            }
            const PoolHash hash = pool.hash();

            db->put(hash.to_binary(), static_cast<uint32_t>(pool.sequence()), pool.to_binary());

            write_queue.pop_front();
        }
    }
}

Storage::Storage()
: d(::std::make_shared<priv>()) {
}

Storage::~Storage() {
}

Storage::Storage(WeakPtr ptr) noexcept
: d(ptr.lock()) {
    if (!d) {
        d = ::std::make_shared<priv>();
    }
}

Storage::WeakPtr Storage::weak_ptr() const noexcept {
    return d;
}

Storage::Error Storage::last_error() const {
    return last_error_map(d.get()).last_error_;
}

::std::string Storage::last_error_message() const {
    const last_error_struct& les = last_error_map(d.get());
    if (!les.last_error_message_.empty()) {
        return les.last_error_message_;
    }
    switch (les.last_error_) {
        case NoError:
            return "No error";
        case NotOpen:
            return "Storage is not open";
        case DatabaseError:
            return "Database error: " + db_last_error_message();
        case ChainError:
            return "Chain integrity error";
        case DataIntegrityError:
            return "Data integrity error";
        case UserCancelled:
            return "Operation cancalled by user";
        case InvalidParameter:
            return "Invalid parameter passed to method.";
        default:
            return "Unknown error";
    }
}

Database::Error Storage::db_last_error() const {
    if (d->db) {
        return d->db->last_error();
    }
    return Database::NotOpen;
}

::std::string Storage::db_last_error_message() const {
    if (d->db) {
        return d->db->last_error_message();
    }
    return ::std::string{"Database not specified"};
}

bool Storage::open(const OpenOptions& opt, OpenCallback callback) {
    if (!opt.db) {
        d->set_last_error(DatabaseError, "No valid database driver specified.");
        return false;
    }

    d->db = opt.db;

    if (!d->db->is_open()) {
        d->set_last_error(DatabaseError, "Error open database: %s", d->db->last_error_message().c_str());
        return false;
    }

    if (opt.newBlockchainTop != cs::kWrongSequence) {
        auto seqToRemove = static_cast<uint32_t>(opt.newBlockchainTop + 1);
		auto seqLast = seqToRemove;
        {
		    Database::IteratorPtr it = d->db->new_iterator();
		    it->seek_to_last();
		    if (it->is_valid()) {
			    auto key = it->key();
			    if (key != std::numeric_limits<uint32_t>::max()) {
				    seqLast = key;
			    }
		    }
        }

		cslog() << "start remove " << seqLast - seqToRemove + 1 << " blocks: " << seqToRemove << " .. " << seqLast;

        while (seqToRemove <= seqLast) {
            cs::Bytes poolBinary;
            if (!d->db->get(seqToRemove++, &poolBinary)) {
                continue;
            }
            auto hash = csdb::Pool::hash_from_binary(std::move(poolBinary));
            if (hash.is_empty()) {
				// try remove another way, using next block's previous hash
				cs::Bytes next_block_bytes;
				const uint32_t next_seq = uint32_t(seqToRemove);
				if (!d->db->get(next_seq, &next_block_bytes)) {
					// cannot, sorry
					return false;
				}
				size_t dummy = 0;
				auto next_pool = csdb::Pool::meta_from_binary(std::move(next_block_bytes), dummy);
				if (!next_pool.is_valid()) {
					// cannot, sorry
					return false;
				}
				if (!d->db->remove(next_pool.previous_hash().to_binary())) {
					// cannot repair, sorry
					return false;
				}
                continue;
            }
            if (!d->db->remove(hash.to_binary())) {
                break;
            }
			cslog() << "block " << seqToRemove - 1 << " is removed";
        }

        return true;
    }

    if (!d->rescan(callback)) {
        d->db.reset();
        return false;
    }

    d->set_last_error();
    return true;
}

bool Storage::open(const ::std::string& path_to_base, OpenCallback callback, cs::Sequence newBlockchainTop) {
    ::std::string path{path_to_base};
    if (path.empty()) {
        path = ::csdb::internal::app_data_path() + "/CREDITS";
    }

    auto db{::std::make_shared<::csdb::DatabaseBerkeleyDB>()};
    db->open(path);

    //d->write_thread = std::thread(&Storage::priv::write_routine, d.get());

    return open(OpenOptions{db, newBlockchainTop}, callback);
}

void Storage::close() {
    d->db.reset();
    d->set_last_error();
}

bool Storage::isOpen() const {
    return ((d->db) && (d->db->is_open()));
}

PoolHash Storage::last_hash() const noexcept {
    std::unique_lock<std::mutex> lock(d->data_lock);
    return d->last_hash;
}

size_t Storage::size() const noexcept {
    std::unique_lock<std::mutex> lock(d->data_lock);
    return d->count_pool;
}

bool Storage::pool_save(Pool pool) {
    if (!isOpen()) {
        d->set_last_error(NotOpen);
        return false;
    }

    if (!pool.is_valid()) {
        d->set_last_error(InvalidParameter, "%s: Invalid pool passed", funcName());
        return false;
    }

    const PoolHash hash = pool.hash();

    if (d->db->get(hash.to_binary())) {
        d->set_last_error(InvalidParameter, "%s: Pool already pressent [hash: %s]", funcName(), hash.to_string().c_str());
        return false;
    }
    /*
      {
        std::unique_lock<std::mutex> lock(d->write_lock);
        d->write_queue.push_back(pool);
        d->write_cond_var.notify_one();
      }
    */
    d->db->put(hash.to_binary(), static_cast<uint32_t>(pool.sequence()), pool.to_binary());

    {
        std::unique_lock<std::mutex> lock(d->data_lock);
        ++d->count_pool;
        if (d->last_hash == pool.previous_hash()) {
            d->last_hash = hash;
        }
    }

    d->pools_cache_insert(pool.sequence(), pool.hash(), pool);

    d->set_last_error();
    return true;
}

Pool Storage::pool_load_internal(const PoolHash& hash, const bool metaOnly, size_t& trxCnt) const {
    if (!isOpen()) {
        d->set_last_error(NotOpen);
        return Pool{};
    }

    if (hash.is_empty()) {
        d->set_last_error(InvalidParameter, "%s: Empty hash passed", funcName());
        return Pool{};
    }

    Pool res;
    bool needParseData = true;
    cs::Bytes data;

    const auto &index = d->pools_cache.get<Storage::priv::PoolElement::byHash>();
    auto it = index.find(hash);
    if (it != index.end()) {
        res = (*it).pool;
        if (!res.is_valid()) {
            d->set_last_error(DataIntegrityError, "%s: Error decoding pool [hash: %s]", funcName(), hash.to_string().c_str());
            return Pool{};
        }
        else {
            d->set_last_error();
        }
        trxCnt = res.transactions().size();
        return res;
    }

    if (!d->db->get(hash.to_binary(), &data)) {
        {
            std::unique_lock<std::mutex> lock2(d->write_lock);
            for (auto& poolToWrite : d->write_queue) {
                if (poolToWrite.hash() == hash) {
                    res = poolToWrite;
                    needParseData = false;
                    trxCnt = res.transactions().size();
                    break;
                }
            }
        }

        if (needParseData && !d->db->get(hash.to_binary(), &data)) {
            d->set_last_error(DatabaseError);
            return Pool{};
        }
    }

    if (needParseData) {
        if (metaOnly) {
            res = Pool::meta_from_binary(std::move(data), trxCnt);
        }
        else {
            res = Pool::from_binary(std::move(data));
            trxCnt = res.transactions().size();
            d->pools_cache_insert(res.sequence(), res.hash(), res);
        }
    }

    if (!res.is_valid()) {
        d->set_last_error(DataIntegrityError, "%s: Error decoding pool [hash: %s]", funcName(), hash.to_string().c_str());
        return Pool{};
    }
    else {
        d->set_last_error();
    }

    return res;
}

bool Storage::write_queue_search(const PoolHash& hash, Pool& res_pool) const {
    std::unique_lock<std::mutex> lock(d->write_lock, std::defer_lock);

    if (!d->write_queue.empty() && lock.try_lock()) {
        auto pos = std::find_if(d->write_queue.begin(), d->write_queue.end(), [&](Pool& pool) { return hash == pool.hash(); });

        if (pos != d->write_queue.cend()) {
            res_pool = *pos;
            return true;
        }
    }
    return false;
}

bool Storage::write_queue_pop(Pool& res_pool) {
    std::unique_lock<std::mutex> lock(d->write_lock);

    if (!d->write_queue.empty()) {
        res_pool = d->write_queue.back();
        d->write_queue.pop_back();
        return true;
    }
    return false;
}

Pool Storage::pool_load(const PoolHash& hash) const {
    size_t size;
    return pool_load_internal(hash, false, size);
}

Pool Storage::pool_load(const cs::Sequence sequence) const {
    if (!isOpen()) {
        d->set_last_error(NotOpen);
        return Pool{};
    }

    Pool res;
    bool needParseData = true;
    cs::Bytes data;

    const auto &index = d->pools_cache.get<Storage::priv::PoolElement::bySequence>();
    auto it = index.find(sequence);
    if (it != index.end()) {
        res = (*it).pool;
        if (!res.is_valid()) {
            d->set_last_error(DataIntegrityError);
            return Pool{};
        }
        else {
            d->set_last_error();
        }
        return res;
    }

    if (!d->db->get(static_cast<uint32_t>(sequence), &data)) {
        {
            std::unique_lock<std::mutex> lock2(d->write_lock);
            for (auto& poolToWrite : d->write_queue) {
                if (poolToWrite.sequence() == sequence) {
                    res = poolToWrite;
                    needParseData = false;
                    break;
                }
            }
        }

        if (needParseData && !d->db->get(static_cast<uint32_t>(sequence), &data)) {
            d->set_last_error(DatabaseError);
            return Pool{};
        }
    }

    if (needParseData) {
        res = Pool::from_binary(std::move(data));
        d->pools_cache_insert(res.sequence(), res.hash(), res);
    }

    if (!res.is_valid()) {
        d->set_last_error(DataIntegrityError);
    }
    else {
        d->set_last_error();
    }

    return res;
}

Pool Storage::pool_load_meta(const PoolHash& hash, size_t& cnt) const {
    if (!isOpen()) {
        d->set_last_error(NotOpen);
        return Pool{};
    }

    if (hash.is_empty()) {
        d->set_last_error(InvalidParameter, "%s: Empty hash passed", funcName());
        return Pool{};
    }

    Pool res{};
    bool found = write_queue_search(hash, res);
    if (found) {
        return res;
    }

    cs::Bytes data;
    if (!d->db->get(hash.to_binary(), &data)) {
        d->set_last_error(DatabaseError);
        return Pool{};
    }

    res = Pool::meta_from_binary(std::move(data), cnt);
    if (!res.is_valid()) {
        d->set_last_error(DataIntegrityError, "%s: Error decoding pool [hash: %s]", funcName(), hash.to_string().c_str());
    }
    else {
        d->set_last_error();
    }

    return res;
}

Pool Storage::pool_remove_last() {
    if (!isOpen()) {
        d->set_last_error(NotOpen);
        return Pool{};
    }

    Pool res{};
    bool found = write_queue_pop(res);

    if (found) {
        d->last_hash = res.previous_hash();
        return res;
    }

    if (last_hash().is_empty()) {
        d->set_last_error(InvalidParameter, "%s: Empty hash passed", funcName());
        return Pool{};
    }

    cs::Bytes data;
    if (!d->db->get(last_hash().to_binary(), &data)) {
        d->set_last_error(DatabaseError);
        return Pool{};
    }

	size_t dummy = 0;
	// need only previous_hash to discover:
	res = Pool::meta_from_binary(std::move(data), dummy);
    if (!res.is_valid()) {
        d->set_last_error(DataIntegrityError, "%s: Error decoding pool meta [hash: %s]", funcName(), last_hash().to_string().c_str());
		return Pool{};
    }
    else {
        d->set_last_error();
    }

	// error nearly impossible
	/*bool ok =*/ d->db->remove(last_hash().to_binary());

    --d->count_pool;
    d->last_hash = res.previous_hash();

    return res;
}

bool Storage::pool_remove_last_repair(cs::Sequence test_sequence, const csdb::PoolHash& test_hash) {
	if (!isOpen()) {
		d->set_last_error(NotOpen);
		return false;
	}
	if (test_sequence == 0) {
		d->set_last_error(InvalidParameter, "%s: Sequence 0 passed", funcName());
		return false;
	}
	if (test_hash.is_empty()) {
		// hash is required!
		d->set_last_error(InvalidParameter, "%s: Empty hash passed", funcName());
		return false;
	}

	// test sequence
	if (test_sequence + 1 != d->count_pool) {
		d->set_last_error(InvalidParameter, "%s: incorrect last sequence passed", funcName());
		return false;
	}

	// clear write_queue if it is not empty
	{
		std::unique_lock<std::mutex> lock(d->write_lock);
		d->write_queue.clear();
	}

	// test hash to conform last sequence or absent at all
	uint32_t tmp;
	if (d->db->seq_no(test_hash.to_binary(), &tmp)) {
		if (tmp != test_sequence) {
			// wrong pair (sequence, hash) passed!
			d->set_last_error(InvalidParameter, "%s: incorrect last hash of %d passed", funcName(), tmp);
			return false;
		}
	}
	else {
		// there is no test_hash in seq_no table, continue with remove operation
	}

	// now we have got correct last sequence and last hash
	// TODO: remove operation fails if test_hash not found in (hash->sequence) table!
	if (!d->db->remove(test_hash.to_binary())) {
		// last error have already set
		return false;
	}

	// setup new last sequence & last hash
	--d->count_pool;
	csdb::Pool last = pool_load(test_sequence - 1);
	if (last.is_valid()) {
		d->last_hash = last.hash();
		return true;
	}
	// previous damaged block found, its hash is unknown
	d->last_hash = csdb::PoolHash{};
	d->set_last_error(DataIntegrityError, "%s: Error loading previous pool %d", funcName(), test_sequence - 1);
	return false;
}

Wallet Storage::wallet(const Address& addr) const {
    return Wallet::get(addr);
}

static bool checkPool(const Pool& pool, const Address& addr,
                      int64_t innerId, Transaction& trx) {
    const auto& trxs = pool.transactions();
    for (const auto& t : trxs) {
        if (t.source() == addr && t.innerID() == innerId) {
            trx = t;
            return true;
        }
    }
    return false;
}

bool Storage::get_from_blockchain(const Address& addr, int64_t innerId,
                                  cs::Sequence lastTrxPs, Transaction& trx) const {
    auto poolSeq = lastTrxPs;
    while (poolSeq != cs::kWrongSequence) {
        if (checkPool(pool_load(poolSeq), addr, innerId, trx)) {
            return true;
        }
        poolSeq = get_previous_transaction_block(addr, poolSeq);
    }
    return false;
}

const ReadBlockSignal& Storage::readBlockEvent() const {
    return d->read_block_event;
}

const BlockReadingStartedSingal& Storage::readingStartedEvent() const {
    return d->start_reading_event;
}

std::vector<Transaction> Storage::transactions(const Address& addr, size_t limit, const TransactionID& offset) const {
    std::vector<Transaction> res;
    res.reserve(limit);

    Pool curPool;
    cs::Sequence curIdx = 0;

    auto seekIt = [this, &curPool, &curIdx](const TransactionID& id) -> bool {
        if (id.is_valid()) {
            curPool = pool_load(id.pool_seq());
            if (curPool.is_valid() && id.index() < curPool.transactions_count()) {
                curIdx = id.index();
                return true;
            }
        }
        return false;
    };

    auto nextIt = [this, &curPool, &curIdx]() -> bool {
        if (curPool.is_valid()) {
            if (curIdx) {
                curIdx--;
                return true;
            }
            else {
                do {
                    curPool = pool_load(curPool.previous_hash());
                } while (curPool.is_valid() && !(curPool.transactions_count()));
                if (curPool.is_valid()) {
                    curIdx = static_cast<cs::Sequence>(curPool.transactions_count() - 1);
                    return true;
                }
            }
        }
        else {
            curPool = pool_load(last_hash());
            while (curPool.is_valid() && !(curPool.transactions_count())) {
                curPool = pool_load(curPool.previous_hash());
            }
            if (curPool.is_valid()) {
                curIdx = static_cast<cs::Sequence>(curPool.transactions_count() - 1);
                return true;
            }
        }
        return false;
    };

    if (offset.is_valid())
        if (!seekIt(offset))
            return res;

    while (res.size() < limit && nextIt()) {
        const Transaction t = curPool.transaction(curIdx);
        if ((t.source() == addr) || (t.target() == addr))
            res.push_back(t);
    }

    return res;
}

Transaction Storage::transaction(const TransactionID& id) const {
    if (!id.is_valid()) {
        d->set_last_error(InvalidParameter, "%s: Transaction id is not valid", __func__);
        return Transaction{};
    }

    return pool_load(id.pool_seq()).transaction(id);
}

Transaction Storage::get_last_by_source(Address source) const noexcept {
    Pool curr = pool_load(last_hash());

    while (curr.is_valid()) {
        const auto& t = curr.get_last_by_source(source);
        if (t.is_valid())
            return t;

        curr = pool_load(curr.previous_hash());
    }

    return Transaction{};
}

Transaction Storage::get_last_by_target(Address target) const noexcept {
    Pool curr = pool_load(last_hash());

    while (curr.is_valid()) {
        const auto& t = curr.get_last_by_target(target);
        if (t.is_valid()) {
            return t;
        }

        curr = pool_load(curr.previous_hash());
    }

    return Transaction{};
}

cs::Bytes Storage::get_trans_index_key(const Address& addr, cs::Sequence seq) {
    ::csdb::priv::obstream os;
    addr.put(os);
    os.put(seq);
    return os.buffer();
}

cs::Sequence Storage::get_previous_transaction_block(const Address& addr, cs::Sequence seq) const {
    cs::Sequence result = cs::kWrongSequence;

    if (!isOpen()) {
        d->set_last_error(NotOpen);
        return result;
    }

    const auto key = get_trans_index_key(addr, seq);
    cs::Bytes data;

    if (d->db->getFromTransIndex(key, &data)) {
        ::csdb::priv::ibstream is(data.data(), data.size());
        is.get(result);
    }

    return result;
}

bool Storage::set_previous_transaction_block(const Address& addr, cs::Sequence currTransBlock, cs::Sequence prevTransBlock) {
    if (!isOpen()) {
        d->set_last_error(NotOpen);
        return false;
    }

    const auto key = get_trans_index_key(addr, currTransBlock);

    ::csdb::priv::obstream os;
    os.put(prevTransBlock);

    return d->db->putToTransIndex(key, os.buffer());
}

bool Storage::remove_last_from_trx_index(const Address& addr, cs::Sequence lastIndexed) {
    if (!isOpen()) {
        d->set_last_error(NotOpen);
        return false;
    }

    return d->db->removeLastFromTrxIndex(get_trans_index_key(addr, lastIndexed));
}

bool Storage::truncate_trxs_index() {
    if (!isOpen()) {
        d->set_last_error(NotOpen);
        return false;
    }
    return d->db->truncateTransIndex();
}

bool Storage::get_contract_data(const Address& abs_addr /*input*/, cs::Bytes& data /*output*/) const {
    const auto& pk = abs_addr.public_key();
    cs::Bytes bytes(pk.size());
    bytes.assign(pk.cbegin(), pk.cend());
    return d->db->getContractData(bytes, data);
}

bool Storage::update_contract_data(const Address& abs_addr /*input*/, const cs::Bytes& data /*input*/) const {
    const auto& pk = abs_addr.public_key();
    cs::Bytes bytes(pk.size());
    bytes.assign(pk.cbegin(), pk.cend());
    return d->db->updateContractData(bytes, data);
}

cs::Sequence Storage::pool_sequence(const PoolHash& hash) const {
    cs::Sequence seq = std::numeric_limits<cs::Sequence>::max();
    if (!isOpen()) {
        d->set_last_error(NotOpen);
        return seq;
    }

    if (hash.is_empty()) {
        d->set_last_error(InvalidParameter, "%s: Empty hash passed", funcName());
        return seq;
    }

    uint32_t tmp;
    if (d->db->seq_no(hash.to_binary(), &tmp)) {
        seq = tmp;
    }
    return seq;
}

csdb::PoolHash Storage::pool_hash(cs::Sequence sequence) const {
    if (!isOpen()) {
        d->set_last_error(NotOpen);
        return PoolHash{};
    }

    Pool res;
    cs::Bytes data;

    if (!d->db->get(static_cast<uint32_t>(sequence), &data)) {
        {
            std::unique_lock<std::mutex> lock2(d->write_lock);
            for (auto& poolToWrite : d->write_queue) {
                if (poolToWrite.sequence() == sequence) {
                    res = poolToWrite;
                    break;
                }
            }
        }

        if (!d->db->get(static_cast<uint32_t>(sequence), &data)) {
            d->set_last_error(DatabaseError);
            return PoolHash{};
        }
    }

    res = Pool::from_binary(std::move(data));
    if (!res.is_valid()) {
        d->set_last_error(DataIntegrityError);
        return PoolHash{};
    }
    else {
        d->set_last_error();
    }

    return res.hash();
}

}  // namespace csdb
