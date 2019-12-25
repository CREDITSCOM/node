/* Send blaming letters to @yrtimd */

#include <algorithm>
#include <iterator>
#include <random>

#include "neighbourhood.hpp"
#include "transport.hpp"
#include "packetvalidator.hpp"

#include <cscrypto/cscrypto.hpp>
#include <csnode/blockchain.hpp>
#include <csnode/configholder.hpp>
#include <lib/system/random.hpp>

namespace {
template <typename T>
T getSecureRandom() {
    T result;
    cscrypto::fillBufWithRandomBytes(static_cast<void*>(&result), sizeof(T));
    return result;
}

template<class InputIt>
std::vector<typename std::iterator_traits<InputIt>::value_type> sample(InputIt first, InputIt last, size_t n) {
    static std::mt19937 engine{std::random_device{}()};

    InputIt nth = next(first, static_cast<std::ptrdiff_t>(n));
    std::vector<typename std::iterator_traits<InputIt>::value_type> result{first, nth};
    size_t k = n + 1;
    for (InputIt it = nth; it != last; ++it, ++k) {
        size_t r = std::uniform_int_distribution<size_t>{0, k}(engine);
        if (r < n)
            result[r] = *it;
    }
    return result;
}

const size_t kNeighborsRedirectMin = 6;
}  // anonimous namespace

Neighbourhood::Neighbourhood(Transport* net)
: transport_(net)
, connectionsAllocator_(MaxConnections + 1)
, nLockFlag_()
, mLockFlag_()
, resqueue(this) {
}

// must work under nLockFlag_
void Neighbourhood::chooseNeighbours() {
    static bool redirectLimit = false;
    static auto startTime = std::chrono::high_resolution_clock::now();

    if (!redirectLimit) {
        auto now_time = std::chrono::high_resolution_clock::now();
        auto spendedTime = std::chrono::duration_cast<std::chrono::seconds>(now_time - startTime);

        if (spendedTime.count() > 10) {    // 10 seconst dry run
            redirectLimit = true;
        }
    }

    size_t redirectNumber = 0;

    if (redirectLimit) {
        redirectNumber = std::max(kNeighborsRedirectMin,
            static_cast<size_t>(
                neighbours_.size() * cs::ConfigHolder::instance().config()->getBroadcastCoefficient()) + 1);

        if (redirectNumber > neighbours_.size()) {
            redirectNumber = neighbours_.size();
        }
    }
    else {
        redirectNumber = neighbours_.size();
    }

    selection_ = sample(std::begin(neighbours_), std::end(neighbours_), redirectNumber);
}

bool Neighbourhood::dispatch(Neighbourhood::BroadPackInfo& bp, bool separate) {
    bool result = false;

    if (bp.sentLastTime) {
        return true;
    }

    if (bp.attempts > MaxResendTimes || !transport_->shouldSendPacket(bp.pack)) {
        return result;
    }

    if (neighbours_.size() == 0) {
        return false;
    }

    bool sent = false;
    for (auto& nb : selection_) {
        bool found = false;
        for (auto ptr = bp.receivers; ptr != bp.recEnd; ++ptr) {
            if (*ptr == nb->id) {
                found = true;
                break;
            }
        }

        if (!found) {
            bool send_to_ss = false;
            if (nb->isSignal) {
                if (!bp.pack.isNetwork()) {
                    const auto type = bp.pack.getType();
                    if (type == MsgTypes::BlockHash) {
                        send_to_ss = true;
                    }
                    else if(type == MsgTypes::RoundTable) {
                        if (transport_->isOwnNodeTrusted()) {
                            send_to_ss = bp.pack.isFragmented() ? bp.pack.getFragmentId() == 0 : true;
                        }
                    }
                }
            }

            auto& dp = msgDirects_.tryStore(bp.pack.getHash());
            dp.pack = bp.pack;
            dp.receiver = nb;

            if (!nb->isSignal || send_to_ss) {
                if (separate) {
                    sent = transport_->sendDirectToSock(&(bp.pack), **nb) || sent;
                } else {
                    sent = transport_->sendDirect(&(bp.pack), **nb) || sent;
                }
            }

            // Assume the SS got this
            if (nb->isSignal) {
                *(bp.recEnd++) = nb->id;
            }
            else {
                result = true;
            }
        }
    }

    if (sent) {
        ++bp.attempts;
        bp.sentLastTime = true;
    }

    return result;
}

bool Neighbourhood::dispatch(Neighbourhood::DirectPackInfo& dp) {
    if (dp.received || dp.attempts > MaxResendTimes) {
        return false;
    }

    if (transport_->sendDirect(&(dp.pack), **dp.receiver)) {
        ++dp.attempts;
    }

    return true;
}

// Not thread safe. Need lock nLockFlag_ above.
void Neighbourhood::sendByNeighbours(const Packet* pack, bool separate) {
    if (pack->isDirect()) {
        for (auto& nb : neighbours_) {
            auto& bp = msgDirects_.tryStore(pack->getHash());

            bp.pack = *pack;
            bp.receiver = nb;

            transport_->sendDirect(pack, **nb);
        }
    }
    else {
        auto& bp = msgBroads_.tryStore(pack->getHash());

        if (!bp.pack) {
            bp.pack = *pack;
        }

        dispatch(bp, separate);
    }
}

void Neighbourhood::sendByConfidant(const Packet* pack, ConnectionPtr conn) {
    auto& bp = msgDirects_.tryStore(pack->getHash());

    bp.pack = *pack;
    bp.receiver = conn;

    transport_->sendDirect(pack, **conn);
}

bool Neighbourhood::canHaveNewConnection() {
    cs::Lock lock(nLockFlag_);
    return isNewConnectionAvailable();
}

void Neighbourhood::checkPending(const uint32_t) {

    {
        cs::Lock lock(nLockFlag_);
        if (enoughConnections()) {
            return;
        }
    }

    cs::Lock lock(mLockFlag_);
    for (auto conn = connections_.begin(); conn != connections_.end(); ++conn) {
        // Attempt to reconnect if the connection hasn't been established yet
        if (!(**conn)->connected && (**conn)->attempts < MaxConnectAttempts) {
            if (transport_->isShouldPending(***conn)) {
                transport_->sendRegistrationRequest(****conn);
            }
        }
    }
}

void Neighbourhood::refreshLimits() {
    cs::Lock lock(nLockFlag_);
    for (auto conn = neighbours_.begin(); conn != neighbours_.end(); ++conn) {
        (*conn)->lastBytesCount.store(0, std::memory_order_relaxed);
    }
}

void Neighbourhood::checkSilent() {
    static uint32_t refillCount = 0;

    bool needRefill = true;
    bool flagCallRefillNeighbourhood{ false };

    { // begin of scoped locked block
        cs::ScopedLock lock(mLockFlag_, nLockFlag_);

        int i = 0;
        std::vector<int> toDisconnect;
        for (auto conn = neighbours_.begin(), end = neighbours_.end(); conn != end; ++conn, ++i) {
            if (!(*conn)->node) {
                toDisconnect.push_back(i);
                continue;
            }

            if ((*conn)->isSignal) {
                continue;
            }

            const auto packetsCount = (*(*conn)->node)->packets.load(std::memory_order_relaxed);

            if (packetsCount == (*conn)->lastPacketsCount) {
                toDisconnect.push_back(i);
            }
            else {
                needRefill = false;
                (*conn)->lastPacketsCount = packetsCount;
            }
        }
        for (auto it = toDisconnect.rbegin(), end = toDisconnect.rend(); it != end; ++it) {
            auto connPtrIt = neighbours_.begin() + *it;
            ConnectionPtr tc = *connPtrIt;

            if (tc->node) {
                cswarning() << "Node " << tc->in << " stopped responding";
                Connection* c = *tc;
                tc->node->connection.compare_exchange_strong(c, nullptr, std::memory_order_release, std::memory_order_relaxed);
            }

            (*connPtrIt)->connected = false;
            (*connPtrIt)->node = RemoteNodePtr();

            neighbours_.erase(connPtrIt);
            chooseNeighbours();
        }

        if (needRefill) {
            ++refillCount;
            if (refillCount >= WarnsBeforeRefill) {
                refillCount = 0;
                flagCallRefillNeighbourhood = true;
            }
        }
        else {
            refillCount = 0;
        }
    } // end of scoped locked block

    if (flagCallRefillNeighbourhood) {
        checkNeighbours();
    }
}

void Neighbourhood::checkNeighbours() {
    bool refill_required = false;
    if (getNeighboursCountWithoutSS() < cs::ConfigHolder::instance().config()->getMinNeighbours()) {
        refill_required = true;
    }
    else {
        if (transport_->requireStartNode()) {
            // look-up starter & count neighbours
            bool starter_found = false;
            // -1 to provide some "rotation" getting free slot
            const uint32_t reqSlotsAvail = 1;
            uint32_t max_cnt_nb = cs::ConfigHolder::instance().config()->getMaxNeighbours() - reqSlotsAvail;
            uint32_t cnt_nb = 0;
            std::list<Connection::Id> to_drop; // if cnt_nb >= max_neighbours, store extra neighbours to drop them at the end
            forEachNeighbour([&](ConnectionPtr ptr) {
                if (ptr->isSignal) {
                    starter_found = true;
                    refill_required = !ptr->connected;
                }
                else {
                    if (++cnt_nb > max_cnt_nb) {
                        to_drop.push_back(ptr->id);
                    }
                }
            });
            if (!to_drop.empty()) {
                // drop ending connections to restrict total count
                csdebug() << "Drop " << to_drop.size() << " connections to provide " << reqSlotsAvail << " available slot(s)";
                for (const auto& id : to_drop) {
                    dropConnection(id);
                }
            }
            if (!starter_found) {
                refill_required = true;
            }
        }
    }
    if (refill_required) {
        transport_->refillNeighbourhood();
    }
}

static ConnectionPtr* findInVec(const Connection::Id& id, std::deque<ConnectionPtr>& vec) {
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        if ((*it)->id == id) {
            return &*it;
        }
    }
    return nullptr;
}

template <typename Vec>
static ConnectionPtr* findInMap(const Connection::Id& id, Vec& vec) {
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        if (it->data->id == id) {
            return &(it->data);
        }
    }

    return nullptr;
}

static ip::udp::endpoint getIndexingEndpoint(const ip::udp::endpoint& ep) {
    if (ep.address().is_v6()) {
        return ep;
    }

    return ip::udp::endpoint(ip::make_address_v6(ip::v4_mapped, ep.address().to_v4()), ep.port());
}

ConnectionPtr Neighbourhood::getConnection(const ip::udp::endpoint& ep) {
    auto& conn = connections_.tryStore(getIndexingEndpoint(ep));

    if (!conn) {
        conn = connectionsAllocator_.emplace();
        conn->in = ep;
    }

    return conn;
}

void Neighbourhood::establishConnection(const ip::udp::endpoint& ep) {
    cs::Lock lock(mLockFlag_);

    if (enoughConnections()) {
        csdebug() << "Connections limit has reached, ignore request connection to " << ep;
        return;
    }

    auto conn = getConnection(ep);

    if (!conn->id) {
        conn->id = getSecureRandom<Connection::Id>();
    }

    if (!conn->connected && transport_->isShouldPending(*conn)) {
        cswarning() << "Establishing connection to " << ep;
        transport_->sendRegistrationRequest(**conn);
    }
}

uint32_t Neighbourhood::size() const {
    cs::Lock lock(nLockFlag_);
    return static_cast<uint32_t>(neighbours_.size());
}

uint32_t Neighbourhood::getNeighboursCountWithoutSS() const {
    cs::Lock lock(nLockFlag_);
    uint32_t count = 0;

    for (auto& nb : neighbours_) {
        if (!nb->isSignal) {
            ++count;
        }
    }

    return count;
}

Connections Neighbourhood::getNeigbours() const {
    Connections connections;
    connections.reserve(neighbours_.size());

    std::copy(std::begin(neighbours_), std::end(neighbours_), std::back_inserter(connections));
    return connections;
}

Connections Neighbourhood::getNeighboursWithoutSS() const {
    Connections connections;
    connections.reserve(neighbours_.size());

    std::copy_if(std::begin(neighbours_), std::end(neighbours_), std::back_inserter(connections), [&](const ConnectionPtr neighbour) {
        return (!neighbour->isSignal);
    });

    return connections;
}

std::unique_lock<std::mutex> Neighbourhood::getNeighboursLock() const {
    return std::unique_lock< std::mutex >(nLockFlag_);
}

void Neighbourhood::forEachNeighbour(std::function<void(ConnectionPtr)> func) {
    cs::Lock lock(nLockFlag_);
    for (const ConnectionPtr& connection : neighbours_) {
        if (connection) {
            func(connection);
        }
    }
}

void Neighbourhood::forEachNeighbourWithoutSS(std::function<void(ConnectionPtr)> func) {
    cs::Lock lock(nLockFlag_);
    for (const ConnectionPtr& connection : neighbours_) {
        if (connection && !connection->isSignal) {
            func(connection);
        }
    }
}

bool Neighbourhood::forRandomNeighbour(std::function<void(ConnectionPtr)> func) {
    ConnectionPtr connection = getRandomNeighbour();

    if (connection.isNull()) {
        return false;
    }

    func(connection);
    return !connection.isNull();
}

void Neighbourhood::addSignalServer(const ip::udp::endpoint& in, const ip::udp::endpoint& out, RemoteNodePtr node) {
    cs::ScopedLock scopeLock(mLockFlag_, nLockFlag_);

    if ((*node)->connection.load(std::memory_order_relaxed)) {
        cserror() << "Connection with the SS node has already been established";
        return;
    }

    ConnectionPtr conn = getConnection(out);

    if (!conn->id) {
        conn->id = getSecureRandom<Connection::Id>();
    }

    conn->in = in;

    if (in != out) {
        conn->specialOut = true;
        conn->out = out;
    }

    conn->isSignal = true;
    // validator have already stored the key: regpack -> validator -> transport (calls to this method)
    conn->key = cs::PacketValidator::instance().getStarterKey();
    connectNode(node, conn);
}

ConnectionPtr Neighbourhood::addConfidant(const ip::udp::endpoint& ep) {
    csdebug() << "Add confidant " << ep;

    cs::ScopedLock scopedLock(mLockFlag_, nLockFlag_);
    auto conn = getConnection(ep);

    if (!conn->id) {
        conn->id = getSecureRandom<Connection::Id>();
    }

    conn->connected = conn->node ? !conn->node->isBlackListed() : false;

    return conn;
}

bool Neighbourhood::updateSignalServer(const ip::udp::endpoint& in) {
    cs::ScopedLock scopeLock(mLockFlag_, nLockFlag_);

    if (auto itServer = std::find_if(neighbours_.begin(), neighbours_.end(), [](auto const& node) {return node->isSignal; }); itServer != neighbours_.end()) {
        itServer->get()->in = in;
        itServer->get()->specialOut = false;
        itServer->get()->out = {};
        return true;
    }
    return false;
}

/* Assuming both the mutexes have been locked */
void Neighbourhood::connectNode(RemoteNodePtr node, ConnectionPtr conn) {
    Connection* connection = nullptr;
    while (!node->connection.compare_exchange_strong(connection, *conn, std::memory_order_release, std::memory_order_relaxed));

    if (connection) {
        auto connPtr = findInVec(connection->id, neighbours_);

        if (connPtr) {
            disconnectNode(connPtr);
        }
    }

    conn->node = node;

    if (conn->connected) {
        csdebug() << "Attempt to connect to already connected " << conn->getOut();
        return;
    }

    conn->connected = true;
    conn->attempts = 0;

    if (enoughConnections()) {
        csdebug() << "Can not add neighbour, neighbours count is equal to max possible neighbours";
        return;
    }
    // to provide some rotation in neighbours_ add to begin, restrict at the end of:
    neighbours_.emplace(neighbours_.cbegin(), conn);
    csdebug() << "Node " << conn->getOut() << " is added to neighbours";
    chooseNeighbours();
}

void Neighbourhood::disconnectNode(ConnectionPtr* connPtr) {
    (*connPtr)->connected = false;
    (*connPtr)->node = RemoteNodePtr();

    auto res = std::find(neighbours_.begin(), neighbours_.end(), *connPtr);

    if (res != neighbours_.end()) {
        neighbours_.erase(res);
        chooseNeighbours();
    }
}

void Neighbourhood::gotRegistration(Connection&& conn, RemoteNodePtr node) {
    cs::ScopedLock scopedLock(mLockFlag_, nLockFlag_);
    ConnectionPtr& connPtr = connections_.tryStore(getIndexingEndpoint(conn.getOut()));

    if (!connPtr) {
        connPtr = connectionsAllocator_.emplace(std::move(conn));
    }
    else {
        if (conn.id < connPtr->id) {
            connPtr->id = conn.id;
        }

        connPtr->key = conn.key;
        connPtr->version = conn.version;

        connPtr->in = conn.in;
        connPtr->specialOut = conn.specialOut;
        connPtr->out = conn.out;
    }

    connectNode(node, connPtr);

    // check if transport available
    if (!transport_->isGood()) {
        cserror() << "Transport is not available!";
        return;
    }

    transport_->sendRegistrationConfirmation(**connPtr, conn.id);
}

void Neighbourhood::gotConfirmation(const Connection::Id& my, const Connection::Id& real, const ip::udp::endpoint& ep, const cs::PublicKey& pk, RemoteNodePtr node) {
    cs::ScopedLock scopedLock(mLockFlag_, nLockFlag_);
    ConnectionPtr* connPtr = findInMap(my, connections_);

    if (!connPtr) {
        cswarning() << "Connection with ID " << my << " not found";
        return;
    }

    if (ep != (*connPtr)->in) {
        (*connPtr)->out = (*connPtr)->in;
        (*connPtr)->specialOut = true;
        (*connPtr)->in = ep;
    }

    (*connPtr)->key = pk;

    if (my != real) {
        (*connPtr)->id = real;
    }

    connectNode(node, *connPtr);
}

bool Neighbourhood::validateConnectionId(RemoteNodePtr node, const Connection::Id id, const ip::udp::endpoint& ep, const cs::PublicKey& pk, const cs::Sequence lastSeq) {
    cs::ScopedLock scopedLock(mLockFlag_, nLockFlag_);

    bool result = true;
    auto realPtr = findInMap(id, connections_);
    auto nConn = node->connection.load(std::memory_order_relaxed);

    if (!realPtr) {
        if (nConn) {
            if (enoughConnections()) {
                cswarning() << "Connections limit has reached, ignore ping from " << ep;
                return false;
            }

            cswarning() << "[NET] got ping from " << ep << " but the remote node is bound to " << nConn->getOut();
            transport_->sendRegistrationRequest(*nConn);
            nConn->lastSeq = lastSeq;
        }
        else {
            cswarning() << "[NET] got ping from " << ep << " but no connection bound, sending refusal";
            Connection conn;
            conn.id = id;
            conn.in = ep;
            conn.specialOut = false;
            transport_->sendRegistrationRefusal(conn, RegistrationRefuseReasons::BadId);
        }

        result = !result;
    }
    else if (realPtr->get() != nConn) {
        if (enoughConnections()) {
            cswarning() << "Connection limit has reached, ignore ping from " << ep;
            return false;
        }

        if (nConn) {
            cswarning() << "[NET] got ping from " << ep << " introduced as " << (*realPtr)->getOut() << " but the remote node is bound to " << nConn->getOut();
            transport_->sendRegistrationRequest(*nConn);
        }
        else {
            cswarning() << "[NET] got ping from " << ep << " introduced as " << (*realPtr)->getOut() << " and there is no bindings, sending reg";
        }

        (*realPtr)->lastSeq = lastSeq;
        (*realPtr)->key = pk;

        connectNode(node, *realPtr);
        transport_->sendRegistrationRequest(***realPtr);
    }
    else {
        (*realPtr)->lastSeq = lastSeq;
    }

    return result;
}

void Neighbourhood::gotRefusal(const Connection::Id& id) {
    cs::ScopedLock scopedLock(mLockFlag_, nLockFlag_);
    auto realPtr = findInMap(id, connections_);

    if (enoughConnections()) {
        return;
    }

    if (realPtr) {
        transport_->sendRegistrationRequest(***realPtr);
    }
}

// thread unsafe, must work under nLockFlag_
bool Neighbourhood::enoughConnections() const {
    const uint32_t conn_limit = std::min(cs::ConfigHolder::instance().config()->getMaxNeighbours(), MaxNeighbours);
    uint32_t count = 0;
    for (auto& nb : neighbours_) {
        if (!nb->isSignal) {
            ++count;
            if (count >= conn_limit) {
                return true;
            }
        }
    }
    return false;
}

bool Neighbourhood::canAddNeighbour() const {
    cs::Lock lock(nLockFlag_);
    return !enoughConnections();
}

void Neighbourhood::gotBadPing(Connection::Id id) {
    cs::ScopedLock scopedLock(mLockFlag_, nLockFlag_);
    auto realPtr = findInMap(id, connections_);

    if (realPtr) {
        transport_->sendRegistrationRefusal(***realPtr, RegistrationRefuseReasons::BadClientVersion);
        disconnectNode(realPtr);
    }
}

bool Neighbourhood::dropConnection(Connection::Id id) {
    cs::ScopedLock scopedLock(mLockFlag_, nLockFlag_);

    auto realPtr = findInMap(id, connections_);
    auto result = realPtr != nullptr;

    if (realPtr) {
        disconnectNode(realPtr);
    }

    return result;
}

// Not thread safe. Need lock nLockFlag_ above.
void Neighbourhood::neighbourHasPacket(RemoteNodePtr node, const cs::Hash& hash) {
    auto conn = node->connection.load(std::memory_order_relaxed);
    if (!conn) {
        return;
    }

    auto& dp = msgDirects_.tryStore(hash);
    dp.received = true;
}

void Neighbourhood::neighbourSentPacket(RemoteNodePtr node, const cs::Hash& hash) {
    cs::Lock lock(nLockFlag_);
    auto connection = node->connection.load(std::memory_order_acquire);

    if (!connection) {
        return;
    }

    Connection::MsgRel& rel = connection->msgRels.tryStore(hash);
    SenderInfo& sInfo = msgSenders_.tryStore(hash);

    rel.needSend = false;

    if (!sInfo.prioritySender) {
        // First time
        auto connPtr = findInVec(connection->id, neighbours_);

        if (connPtr) {
            sInfo.prioritySender = *connPtr;

            rel.acceptOrder = sInfo.totalSenders++;

            for (auto& nb : neighbours_) {
                if (nb->id != connection->id) {
                    transport_->sendPackRenounce(hash, **nb);
                }
            }
        }
    }
    else if (*sInfo.prioritySender != connection) {
        if (!rel.acceptOrder) {
            rel.acceptOrder = ++sInfo.totalSenders;
        }

        transport_->sendPackRenounce(hash, *connection);
    }
}

void Neighbourhood::neighbourSentRenounce(RemoteNodePtr node, const cs::Hash& hash) {
    cs::Lock lock(nLockFlag_);
    auto connection = node->connection.load(std::memory_order_acquire);

    if (connection) {
        SenderInfo& si = msgSenders_.tryStore(hash);
        Connection::MsgRel& rel = connection->msgRels.tryStore(hash);
        rel.acceptOrder = si.totalSenders++;
        rel.needSend = false;
    }
}

void Neighbourhood::redirectByNeighbours(const Packet* pack) {
    cs::Lock lock(nLockFlag_);

    for (auto& nb : neighbours_) {
        Connection::MsgRel& rel = nb->msgRels.tryStore(pack->getHeaderHash());
        if (rel.needSend) {
            transport_->sendDirect(pack, **nb);
        }
    }
}

void Neighbourhood::pingNeighbours() {
    const uint32_t max_cnt = cs::ConfigHolder::instance().config()->getMaxNeighbours();
    const bool limit_cnt_ping = cs::ConfigHolder::instance().config()->restrictNeighbours();

    cs::Lock lock(nLockFlag_);

    uint32_t cnt_ping = 0;
    for (auto& nb : neighbours_) {
        if (limit_cnt_ping) {
            // stop ping on max neighbours count, then connection will close automatically, ending items will be restricted such a way
            if (!nb->isSignal) {
                ++cnt_ping;
                if (cnt_ping > max_cnt) {
                    csdebug() << "Connections limit " << max_cnt << " has reached, ignore the rest neighbours";
                    break;
                }
            }
        }
        transport_->sendPingPack(**nb);
    }
}

bool Neighbourhood::isPingDone() {
    cs::Lock lock(nLockFlag_);

    for (auto& nb : neighbours_) {
        if (nb->lastSeq) {
            return true;
        }
    }

    return false;
}

void Neighbourhood::resendPackets() {
    cs::Lock lock(nLockFlag_);
    for (auto& bp : msgBroads_) {
        if (!bp.data.pack) {
            continue;
        }

        if (!dispatch(bp.data)) {
            bp.data.pack = Packet();
        }
        bp.data.sentLastTime = false;
    }
}

ConnectionPtr Neighbourhood::getConnection(const RemoteNodePtr node) {
    cs::Lock lock(nLockFlag_);
    Connection* conn = node->connection.load(std::memory_order_acquire);

    if (!conn) {
        return ConnectionPtr();
    }

    auto cPtr = findInVec(conn->id, neighbours_);

    if (cPtr) {
        return *cPtr;
    }

    return ConnectionPtr();
}

ConnectionPtr Neighbourhood::getNextRequestee(const cs::Hash& hash) {
    cs::Lock lock(nLockFlag_);

    SenderInfo& si = msgSenders_.tryStore(hash);
    ++si.reaskTimes;

    if (si.totalSenders < si.reaskTimes) {
        si.reaskTimes = 0;
        return si.prioritySender;
    }

    for (auto& nb : neighbours_) {
        if (nb->isSignal) {
            continue;
        }

        Connection::MsgRel& rel = nb->msgRels.tryStore(hash);

        if (rel.acceptOrder == si.reaskTimes) {
            return nb;
        }
    }

    return si.prioritySender;
}

ConnectionPtr Neighbourhood::getNeighbour(const std::size_t number) {
    cs::Lock lock(nLockFlag_);

    if (number >= neighbours_.size()) {
        return ConnectionPtr();
    }

    ConnectionPtr candidate = *(neighbours_.begin() + static_cast<std::ptrdiff_t>(number));

    if (!candidate) {
        return ConnectionPtr();
    }

    return candidate;
}

ConnectionPtr Neighbourhood::getNeighbourByKey(const cs::PublicKey& pk) {
    cs::Lock lock(nLockFlag_);

    for (auto& nb : neighbours_) {
        if (nb->key == pk) {
            return nb;
        }
    }

    return ConnectionPtr();
}

void Neighbourhood::registerDirect(const Packet* packPtr, ConnectionPtr conn) {
    cs::ScopedLock scopedLock(mLockFlag_, nLockFlag_);

    auto& bp = msgDirects_.tryStore(packPtr->getHash());
    bp.pack = *packPtr;
    bp.receiver = conn;
}

bool Neighbourhood::isNewConnectionAvailable() const {
    return neighbours_.size() < MaxNeighbours;
}

ConnectionPtr Neighbourhood::getRandomNeighbour() {
    cs::Lock lock(nLockFlag_);
    if (neighbours_.size() == 0) {
        return ConnectionPtr();
    }

    const size_t neighbourCount = static_cast<size_t>(neighbours_.size() - 1U);
    const int randomNumber = cs::Random::generateValue<int>(0, static_cast<int>(neighbourCount));

    return *(neighbours_.begin() + randomNumber);
}

bool Neighbourhood::ResendQueue::insert(Packet pack, ConnectionPtr conn) {
    cs::Lock lock(qLock);

    static int count = 0;

    DirectPackInfo info;
    info.pack = pack;
    info.receiver = conn;
    info.startTPoint = std::chrono::system_clock::now();
    info.tpoint = info.startTPoint;
    info.mixHash = pack.getHash();
    makeMixHash(info.mixHash, (**conn).id);
    auto res = packetsRef.insert(std::make_pair(info.mixHash, nullptr));
    if (res.second) {
        packets.push(info);
        res.first->second = &packets.back();
        return true;
    }
    return false;
}

void Neighbourhood::ResendQueue::remove(const cs::Hash& hash, Connection* conn) {
    cs::Lock lock(qLock);
    static int count = 0;

    cs::Hash mixHash = hash;
    makeMixHash(mixHash, conn->id);
    auto it = packetsRef.find(mixHash);
    if (it != packetsRef.end()) {
        it->second->received = true;
    }
}

void Neighbourhood::ResendQueue::resend() {
    std::vector<DirectPackInfo *> toSend;
    {
        cs::Lock lock(qLock);
        auto now = std::chrono::system_clock::now();
        while (!packets.empty()) {
            auto& packinfo = packets.front();
            std::chrono::duration<double> diff = now - packinfo.tpoint;
            if (diff.count() > resendPeriod) {
                std::chrono::duration<double> diffAtStart = now - packinfo.startTPoint;
                if (diffAtStart.count() > resendTimeout) {
                    packetsRef.erase(packinfo.mixHash);
                    packets.pop();
                    continue;
                }
                if (!packinfo.received) {
                    auto tmp = packinfo;
                    tmp.tpoint = now;
                    packetsRef.erase(packinfo.mixHash);
                    packets.pop();
                    auto res = packetsRef.insert(std::make_pair(tmp.mixHash, nullptr));
                    packets.push(tmp);
                    res.first->second = &packets.back();
                    toSend.push_back(&packets.back());
                } else {
                    packetsRef.erase(packinfo.mixHash);
                    packets.pop();
                }
                continue;
            }
            break;
        }
    }
    for (auto out: toSend) {
        nh->transport_->sendDirect(&out->pack, **out->receiver);
    }
}
