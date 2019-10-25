/* Send blaming letters to @yrtimd */
#ifndef NEIGHBOURHOOD_HPP
#define NEIGHBOURHOOD_HPP

#include <deque>

#include <boost/asio.hpp>

#include <lib/system/allocators.hpp>
#include <lib/system/cache.hpp>
#include <lib/system/common.hpp>

#include "packet.hpp"

namespace ip = boost::asio::ip;

class Network;
class Transport;

class BlockChain;

const uint32_t MaxMessagesToKeep = 128;
const uint32_t MaxResendTimes =
#if defined(WEB_WALLET_NODE)
8;
#else
4;
#endif // !WEB_WALLET_NODE
const uint32_t WarnsBeforeRefill = 8;

struct Connection;
struct RemoteNode {
    __cacheline_aligned std::atomic<uint64_t> packets = {0};

    __cacheline_aligned std::atomic<uint32_t> strikes = {0};
    __cacheline_aligned std::atomic<bool> blackListed = {ATOMIC_FLAG_INIT};

    void addStrike() {
        strikes.fetch_add(1, std::memory_order_relaxed);
    }

    void setBlackListed(bool b) {
        blackListed.store(b, std::memory_order_relaxed);
    }

    bool isBlackListed() {
        return blackListed.load(std::memory_order_relaxed);
    }

    __cacheline_aligned std::atomic<Connection*> connection = {nullptr};
};

using RemoteNodePtr = MemPtr<TypedSlot<RemoteNode>>;

struct Connection {
    typedef uint64_t Id;

    Connection() = default;

    Connection(Connection&& rhs)
    : id(rhs.id)
    , version(rhs.version)
    , lastBytesCount(rhs.lastBytesCount.load(std::memory_order_relaxed))
    , lastPacketsCount(rhs.lastPacketsCount)
    , attempts(rhs.attempts)
    , key(rhs.key)
    , in(std::move(rhs.in))
    , specialOut(rhs.specialOut)
    , out(std::move(rhs.out))
    , node(std::move(rhs.node))
    , isSignal(rhs.isSignal)
    , connected(rhs.connected)
    , msgRels(std::move(rhs.msgRels)) {
    }

    Connection(const Connection&) = delete;
    ~Connection() {
    }

    const ip::udp::endpoint& getOut() const {
        return specialOut ? out : in;
    }

    Id id = 0;
    cs::Version version = 0;

    static const uint32_t BytesLimit = 1 << 20;
    mutable std::atomic<uint32_t> lastBytesCount = {0};

    uint64_t lastPacketsCount = 0;
    uint32_t attempts = 0;

    cs::PublicKey key;
    ip::udp::endpoint in;

    bool specialOut = false;
    ip::udp::endpoint out;

    RemoteNodePtr node;

    bool isSignal = false;
    bool connected = false;

    struct MsgRel {
        uint32_t acceptOrder = 0;
        bool needSend = true;
    };

    FixedHashMap<cs::Hash, MsgRel, uint16_t, MaxMessagesToKeep> msgRels;

    cs::Sequence lastSeq = 0;

    bool operator!=(const Connection& rhs) const {
        return id != rhs.id || key != rhs.key || in != rhs.in || specialOut != rhs.specialOut || (specialOut && out != rhs.out) ||
               version != rhs.version;
    }
};

using ConnectionPtr = MemPtr<TypedSlot<Connection>>;
using Connections = std::vector<ConnectionPtr>;

class Neighbourhood {
public:
    const static uint32_t MinConnections = 1;
    const static uint32_t MaxConnections = 1024;
    const static uint32_t MaxNeighbours = 256;
    const static uint32_t MinNeighbours = 3;
    const static uint32_t MaxConnectAttempts = 64;

};

#endif  // NEIGHBOURHOOD_HPP
