/**
 * @file database.h
 * @author Roman Bukin, Evgeny Zalivochkin
 */

#ifndef _CREDITS_CSDB_DATABASE_H_INCLUDED_
#define _CREDITS_CSDB_DATABASE_H_INCLUDED_

#include <client/params.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <csdb/internal/types.hpp>

namespace csdb {
class Database {
public:
    enum Error {
        NoError = 0,
        NotFound = 1,
        Corruption = 2,
        NotSupported = 3,
        InvalidArgument = 4,
        IOError = 5,
        NotOpen = 6,
        UnknownError = 255,
    };

protected:
    Database();

public:
    virtual ~Database();

    virtual bool is_open() const = 0;
    virtual bool put(const cs::Bytes& key, uint32_t seq_no, const cs::Bytes& value) = 0;
    virtual bool get(const cs::Bytes& key, cs::Bytes* value = nullptr) = 0;
    virtual bool get(const uint32_t seq_no, cs::Bytes* value = nullptr) = 0;
    virtual bool remove(const cs::Bytes& key) = 0;
    virtual bool seq_no(const cs::Bytes& key, uint32_t* value) = 0; // sequnce from block hash

    using Item = std::pair<cs::Bytes, cs::Bytes>;
    using ItemList = std::vector<Item>;
    virtual bool write_batch(const ItemList& items) = 0;

    virtual bool putToTransIndex(const cs::Bytes& key, const cs::Bytes& value) = 0;
    virtual bool getFromTransIndex(const cs::Bytes& key, cs::Bytes* value) = 0;
    virtual void truncateTransIndex() = 0;

    virtual bool updateContractData(const cs::Bytes& key, const cs::Bytes& data) = 0;
    virtual bool getContractData(const cs::Bytes& key, cs::Bytes& data) = 0;

    class Iterator {
    protected:
        Iterator();

    public:
        virtual ~Iterator();

    public:
        virtual bool is_valid() const = 0;
        virtual void seek_to_first() = 0;
        virtual void seek_to_last() = 0;
        virtual void seek(const cs::Bytes& key) = 0;
        virtual void next() = 0;
        virtual void prev() = 0;
        virtual cs::Bytes key() const = 0;
        virtual cs::Bytes value() const = 0;
    };
    using IteratorPtr = std::shared_ptr<Iterator>;
    virtual IteratorPtr new_iterator() = 0;

public:
    Error last_error() const;
    std::string last_error_message() const;

protected:
    void set_last_error(Error error = NoError, const std::string& message = std::string());
    void set_last_error(Error error, const char* message, ...);
};

}  // namespace csdb

#endif  // _CREDITS_CSDB_DATABASE_H_INCLUDED_
