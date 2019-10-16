/* Send blaming letters to @yrtimd */

#include <algorithm>
#include <iterator>
#include <random>

#include "neighbourhood.hpp"
#include "transport.hpp"

#include <cscrypto/cscrypto.hpp>
#include <csnode/blockchain.hpp>
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
, mLockFlag_() {
}

void Neighbourhood::chooseNeighbours() {
    static bool redirectLimit = false;
    static auto startTime = std::chrono::high_resolution_clock::now();

    if (!redirectLimit) {
        auto now_time = std::chrono::high_resolution_clock::now();
        auto spended_time = std::chrono::duration_cast<std::chrono::seconds>(now_time - startTime);
        if (spended_time.count() > 10) redirectLimit = true; // 10 seconst dry run
    }

    size_t redirectNumber;
    if (redirectLimit) {
        redirectNumber = std::max(kNeighborsRedirectMin, neighbours_.size() / 3 + 1);
        if (redirectNumber > neighbours_.size()) {
            redirectNumber = neighbours_.size();
        }
    } else {
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

    if (neighbours_.size() == 0) return false;

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

    if (pack->isNeighbors()) {
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

void Neighbourhood::sendByConfidants(const Packet* pack) {
    for (auto& nb : confidants_) {
        auto& bp = msgDirects_.tryStore(pack->getHash());

        bp.pack = *pack;
        bp.receiver = nb;

        transport_->sendDirect(pack, **nb);
    }
}

bool Neighbourhood::canHaveNewConnection() {
    cs::Lock lock(nLockFlag_);
    return neighbours_.size() < MaxNeighbours;
}

void Neighbourhood::checkPending(const uint32_t) {
    cs::Lock lock(mLockFlag_);
    for (auto conn = connections_.begin(); conn != connections_.end(); ++conn) {
        // Attempt to reconnect if the connection hasn't been established yet
        if (!(**conn)->connected && (**conn)->attempts < MaxConnectAttempts) {
            transport_->sendRegistrationRequest(****conn);
        }
    }
}

void Neighbourhood::refreshLimits() {
    cs::Lock lock(nLockFlag_);
    for (auto conn = neighbours_.begin(); conn != neighbours_.end(); ++conn) {
//        for (cs::Sequence i = 0; i < BlocksToSync; ++i) {
//            if (++((*conn)->syncSeqsRetries[i]) >= MaxSyncAttempts) {
//                (*conn)->syncSeqs[i] = 0;
//                (*conn)->syncSeqsRetries[i] = 0;
//            }
//        }

        (*conn)->lastBytesCount.store(0, std::memory_order_relaxed);
    }
}

void Neighbourhood::checkSilent() {
    static uint32_t refillCount = 0;

    bool needRefill = true;
    std::atomic_bool flagCallRefillNeighbourhood{ false };

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
        transport_->refillNeighbourhood();
    }
}

void Neighbourhood::checkNeighbours() {
    uint32_t size = 0;

    {
        cs::Lock lock(nLockFlag_);
        size = uint32_t(neighbours_.size());
    }

    if (size < MinNeighbours) {
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
    cswarning() << "Getting connection";
    auto& conn = connections_.tryStore(getIndexingEndpoint(ep));

    if (!conn) {
        conn = connectionsAllocator_.emplace();
        conn->in = ep;
    }

    return conn;
}

void Neighbourhood::establishConnection(const ip::udp::endpoint& ep) {
    cswarning() << "Establishing connection to " << ep;

    cs::Lock lp(mLockFlag_);
    auto conn = getConnection(ep);

    if (!conn->id) {
        conn->id = getSecureRandom<Connection::Id>();
    }

    if (!conn->connected) {
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

    std::copy_if(std::begin(neighbours_), std::end(neighbours_), std::back_inserter(connections), [&](const ConnectionPtr neighbour) { return (!neighbour->isSignal); });

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
    cs::Lock lock(nLockFlag_);
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
    connectNode(node, conn);
}

ConnectionPtr Neighbourhood::addConfidant(const ip::udp::endpoint& ep, bool insert) {
    csdebug() << "Add confidant " << ep;

    cs::ScopedLock scopedLock(mLockFlag_, nLockFlag_);
    auto conn = getConnection(ep);

    if (!conn->id) {
        conn->id = getSecureRandom<Connection::Id>();
    }

    conn->connected = true;
    if (insert) {
        confidants_.push_back(conn);
    }
    return conn;
}

void Neighbourhood::removeConfidants() {
    cs::ScopedLock scopedLock(mLockFlag_, nLockFlag_);
    confidants_.clear();
}

bool Neighbourhood::updateSignalServer(const ip::udp::endpoint& in) {
    cs::ScopedLock scopeLock(mLockFlag_, nLockFlag_); // #!
    
    if (auto itServer = std::find_if(neighbours_.begin(), neighbours_.end(), [](auto const& node) { return node->isSignal; }); itServer != neighbours_.end()) {
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
        return;
    }

    conn->connected = true;
    conn->attempts = 0;

    if (!isNewConnectionAvailable()) {
        cswarning() << "Can not add neighbour, neighbours size is equal to max possible neighbours";
        return;
    }

    neighbours_.emplace(neighbours_.end(), conn);
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

    if (realPtr) {
        transport_->sendRegistrationRequest(***realPtr);
    }
}


// Not thread safe. Need lock nLockFlag_ above.
void Neighbourhood::neighbourHasPacket(RemoteNodePtr node, const cs::Hash& hash, const bool isDirect) {
    auto conn = node->connection.load(std::memory_order_relaxed);
    if (!conn) {
        return;
    }

    if (isDirect) {
        auto& dp = msgDirects_.tryStore(hash);
        dp.received = true;
    }
    else {
        auto& bp = msgBroads_.tryStore(hash);

        for (auto ptr = bp.receivers; ptr != bp.recEnd; ++ptr) {
            if (*ptr == conn->id) {
                return;
            }
        }

        if ((bp.recEnd - bp.receivers) < MaxNeighbours) {
            *(bp.recEnd++) = conn->id;
        }
    }
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
    cs::Lock lock(nLockFlag_);

    for (auto& nb : neighbours_) {
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
    uint32_t cnt1 = 0;
    uint32_t cnt2 = 0;

    for (auto& bp : msgBroads_) {
        if (!bp.data.pack) {
            continue;
        }

        if (!dispatch(bp.data)) {
            bp.data.pack = Packet();
        }
        else {
            ++cnt1;
        }

        bp.data.sentLastTime = false;
    }

    for (auto& dp : msgDirects_) {
        if (!dp.data.pack) {
            continue;
        }

        if (!dispatch(dp.data)) {
            dp.data.pack = Packet();
        }
        else {
            ++cnt2;
        }
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

ConnectionPtr Neighbourhood::getRandomSyncNeighbour() {
    cs::Lock lock(nLockFlag_);

    const int candidateNumber = getRandomSyncNeighbourNumber();

    if (candidateNumber < 0) {
        return ConnectionPtr();
    }

    ConnectionPtr candidate = *(neighbours_.begin() + candidateNumber);

    if (!candidate->syncNeighbourRetries) {
        candidate->syncNeighbourRetries = cs::Random::generateValue<uint32_t>(1, MaxSyncAttempts * 3);
    }

    --(candidate->syncNeighbourRetries);

    if (candidate->syncNeighbourRetries == 0) {
        candidate->isRequested = true;
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

void Neighbourhood::resetSyncNeighbours() {
    for (auto& nb : neighbours_) {
        nb->isRequested = false;
        nb->syncNeighbourRetries = 0;
    }
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

int Neighbourhood::getRandomSyncNeighbourNumber(const std::size_t attemptCount) {
    if (neighbours_.size() == 0) {
        cslog() << "Neighbourhood, no neighbours";
        return -1;
    }

    const size_t neighbourCount = static_cast<size_t>(neighbours_.size() - 1U);

    if (attemptCount > (neighbourCount * 3)) {
        int index = 0;
        for (const auto& nb : neighbours_) {
            if (nb->isSignal || nb->isRequested) {
                ++index;
            }
            else {
                return index;
            }
        }
        return -1;
    }

    const int randomNumber = cs::Random::generateValue<int>(0, static_cast<int>(neighbourCount));
    const ConnectionPtr nb = *(neighbours_.begin() + randomNumber);

    if (!nb) {
        return -1;
    }

    if (nb->isSignal || nb->isRequested) {
        return getRandomSyncNeighbourNumber(attemptCount + 1);
    }

    return randomNumber;
}

ConnectionPtr Neighbourhood::getRandomNeighbour() {
    if (neighbours_.size() == 0) {
        return ConnectionPtr();
    }

    const size_t neighbourCount = static_cast<size_t>(neighbours_.size() - 1U);
    const int randomNumber = cs::Random::generateValue<int>(0, static_cast<int>(neighbourCount));

    return *(neighbours_.begin() + randomNumber);
}
