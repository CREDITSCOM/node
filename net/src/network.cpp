/* Send blaming letters to @yrtimd */
#include <chrono>
#include <lib/system/utils.hpp>
#include <net/logger.hpp>
#include <thread>

#ifdef __linux__
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <vector>
#endif

#ifdef __APPLE__
#include <sys/time.h>
#endif

#include <net/packetvalidator.hpp>
#include "network.hpp"
#include "transport.hpp"

#include <set>

// disables 10052, 10054 win socket errors
#if defined(WIN32)
#define DISABLE_WIN_SOCKET_ERRORS
#endif

using boost::asio::buffer;

const ip::udp::socket::message_flags NO_FLAGS = 0;

static ip::udp::socket bindSocket(io_context& context, Network* net, const EndpointData& data, bool ipv6 = true) {
    try {
        ip::udp::socket sock(context, ipv6 ? ip::udp::v6() : ip::udp::v4());

        if (ipv6) {
            sock.set_option(ip::v6_only(false));
        }

        sock.set_option(ip::udp::socket::reuse_address(true));
#ifndef __APPLE__
        sock.set_option(ip::udp::socket::send_buffer_size(1 << 23));
        sock.set_option(ip::udp::socket::receive_buffer_size(1 << 23));
#endif

#ifdef DISABLE_WIN_SOCKET_ERRORS
        BOOL bNewBehavior = FALSE;
        DWORD dwBytesReturned = 0;

        WSAIoctl(sock.native_handle(), SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior), nullptr, 0, &dwBytesReturned, nullptr, nullptr);
        WSAIoctl(sock.native_handle(), SIO_UDP_NETRESET, &bNewBehavior, sizeof(bNewBehavior), nullptr, 0, &dwBytesReturned, nullptr, nullptr);
#endif
        if (data.ipSpecified) {
            auto ep = net->resolve(data);
            sock.bind(ep);
        }
        else {
            sock.bind(ip::udp::endpoint(ipv6 ? ip::udp::v6() : ip::udp::v4(), data.port));
        }

        return sock;
    }
    catch (boost::system::system_error& e) {
        cserror() << "Cannot bind socket on " << e.what();
        return ip::udp::socket(context);
    }
}  //  bindSocket

ip::udp::endpoint Network::resolve(const EndpointData& data) {
    return ip::udp::endpoint(data.ip, data.port);
}

ip::udp::socket* Network::getSocketInThread(const bool openOwn, const EndpointData& epd, std::atomic<Network::ThreadStatus>& status, const bool ipv6) {
    ip::udp::socket* result = nullptr;

    if (openOwn) {
        result = new ip::udp::socket(bindSocket(context_, this, epd, ipv6));

        if (!result->is_open()) {
            result = nullptr;
        }
    }
    else {
        while (!singleSockOpened_.load())
            ;
        result = singleSock_.load();
    }

    status.store(result ? ThreadStatus::Success : ThreadStatus::Failed);

    return result;
}  // resolve

void Network::readerRoutine(const Config& config) {
    ip::udp::socket* sock = getSocketInThread(config.hasTwoSockets(), config.getInputEndpoint(), readerStatus_, config.useIPv6());

    if (!sock) {
        return;
    }

    while (!initFlag_.load()) {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(1s);
    }

    boost::system::error_code lastError;
    size_t packetSize;

    while (stopReaderRoutine == false) {  // changed from true
        auto& task = iPacMan_.allocNext();

        if (stopReaderRoutine) {
            return;
        }

        std::atomic_thread_fence(std::memory_order_acq_rel);
        while (!task.pack.data_.ptr_) {
            cslog() << "net: invalid input packet!!!!!!!!!";
        }
        packetSize = sock->receive_from(buffer(task.pack.data(), Packet::MaxSize), task.sender, NO_FLAGS, lastError);
        while (!task.pack.data_.ptr_) {
            cslog() << "net: invalid input packet!!!!!!!!!";
        }

        if (!lastError) {
            task.size = task.pack.decode(packetSize);  // try to decode first

            bool reject = false;
            if (task.size == 0) {
                cswarning() << "Ignore incorrect packet fragment, drop";
                reject = true;
            }
            else if (!task.pack.hasValidFragmentation()) {
                cswarning() << "Incorrect fragment identity in message or too many fragments, drop (" << task.pack.getFragmentId() << " from " << task.pack.getFragmentsNum()
                            << "), sender " << task.sender;
                reject = true;
            }

            if (reject) {
                iPacMan_.rejectLast();
            }
            else {
                iPacMan_.enQueueLast();

#ifdef LOG_NET
                csdebug(logger::Net) << "<-- " << packetSize << " bytes from " << task.sender << " " << task.pack;
#endif
            }
#ifdef __linux__
            static uint64_t one = 1;
            write(readerEventfd_, &one, sizeof(uint64_t));
#endif
#if defined(WIN32) || defined(__APPLE__)
            while (readerLock.test_and_set(std::memory_order_acquire))  // acquire lock
                ;                                                       // spin
            readerTaskCount_.fetch_add(1, std::memory_order_relaxed);
#ifdef WIN32
            SetEvent(readerEvent_);
#else
            kevent(readerKq_, &readerEvent_, 1, NULL, 0, NULL);
#endif
            readerLock.clear(std::memory_order_release);  // release lock
#endif
        }
        else {
            cserror() << "Cannot receive packet. Error " << lastError;
            iPacMan_.rejectLast();
        }
    }

    cswarning() << "readerRoutine STOPPED!!!\n";
}

static inline void sendPack(ip::udp::socket& sock, TaskPtr<OPacMan>& task, const ip::udp::endpoint& ep) {
    boost::system::error_code lastError;
    size_t size = 0;
    size_t encodedSize = 0;

    uint32_t cnt = 0;

    // net code was built on this constant (Packet::MaxSize)
    // and is used it implicitly in a lot of places(
    char packetBuffer[Packet::MaxSize];
    boost::asio::mutable_buffer encodedPacket = task->pack.encode(buffer(packetBuffer, sizeof(packetBuffer)));
    encodedSize = encodedPacket.size();
    do {
        size = sock.send_to(encodedPacket, ep, NO_FLAGS, lastError);

        if (++cnt == 10) {
            cnt = 0;
            std::this_thread::yield();
        }
    } while (lastError == boost::asio::error::would_block);

    if (lastError || size < encodedSize) {
        cserror() << "Cannot send packet. Error " << lastError;
    }
#ifdef LOG_NET
    else {
        csdebug(logger::Net) << "--> " << size << " bytes to " << ep << " " << task->pack;
    }
#endif
}

void Network::writerRoutine(const Config& config) {
    ip::udp::socket* sock = getSocketInThread(config.hasTwoSockets(), config.getOutputEndpoint(), writerStatus_, config.useIPv6());

    if (!sock) {
        return;
    }
#ifdef __linux__
    std::vector<struct mmsghdr> msg;
    std::vector<struct iovec> iovecs;
    std::vector<std::array<char, Packet::MaxSize>> packets_buffer;
    std::vector<boost::asio::mutable_buffer> encoded_packets;
    std::vector<ip::udp::endpoint> endpoints;
#endif
    while (stopWriterRoutine == false) {  // changed from true
#ifdef __linux__
        uint64_t tasks;
        int s = read(writerEventfd_, &tasks, sizeof(uint64_t));
        if (s != sizeof(uint64_t))
            continue;

        if (tasks > 200) {
            cslog() << "strange: too many tasks " << tasks;
        }

        msg.resize(tasks);
        std::fill(msg.begin(), msg.end(), mmsghdr{});
        iovecs.resize(tasks);
        std::fill(iovecs.begin(), iovecs.end(), iovec{});
        packets_buffer.resize(tasks);
        endpoints.resize(tasks);
        encoded_packets.clear();

        int j = 0;
        for (uint64_t i = 0; i < tasks; i++) {
            auto task = oPacMan_.getNextTask();
            std::atomic_thread_fence(std::memory_order_acquire);
            while (!task->pack.data_.ptr_) {
                cslog() << "net: invalid packet for send!!!!!!!!!";
            }
            encoded_packets.emplace_back(task->pack.encode(buffer(packets_buffer[j].data(), Packet::MaxSize)));
            endpoints[j] = task->endpoint;
            iovecs[j].iov_base = encoded_packets[j].data();
            iovecs[j].iov_len = encoded_packets[j].size();
            msg[j].msg_hdr.msg_iov = &iovecs[j];
            msg[j].msg_hdr.msg_iovlen = 1;
            msg[j].msg_hdr.msg_name = endpoints[j].data();
            msg[j].msg_hdr.msg_namelen = endpoints[j].size();
            ++j;
        }
        if (j == 0)
            continue;
        tasks = j;

        int sended = 0;
        struct mmsghdr* messages = msg.data();
        do {
            sended = sendmmsg(sock->native_handle(), messages, tasks, 0);
            if (sended < 0) {
                cslog() << "sendmmsg errno = " << errno;
                if (errno != EAGAIN)
                    break;
            }
            messages += sended;
            tasks -= sended;
        } while (tasks);
#endif
#if defined(WIN32) || defined(__APPLE__)
#ifdef WIN32
        WaitForSingleObject(writerEvent_, INFINITE);
#else
        struct kevent event;
        kevent(writerKq_, NULL, 0, &event, 1, NULL);
#endif
        while (writerLock.test_and_set(std::memory_order_acquire))  // acquire lock
            ;                                                       // spin
        int tasks = writerTaskCount_;
        writerTaskCount_ = 0;
        writerLock.clear(std::memory_order_release);  // release lock

        for (int i = 0; i < tasks; i++) {
            auto task = oPacMan_.getNextTask();
            while (!task->pack.data_.ptr_) {
                cslog() << "net: invalid packet!!!!!!!!!";
            }
            sendPack(*sock, task, task->endpoint);
        }
#endif
    }

    cswarning() << "writerRoutine STOPPED!!!\n";
}

// Processors
void Network::processorRoutine() {
    CallsQueue& externals = CallsQueue::instance();
#ifdef __linux__
    struct pollfd pfd {};
    pfd.fd = readerEventfd_;
    pfd.events = POLLIN;
    constexpr int timeout = 50;  // 50ms
#elif __APPLE__
    struct timespec timeout {
        0, 50000000
    };                                             // 50ms
#endif

    while (stopProcessorRoutine == false) {
        externals.callAll();
#ifdef __linux__
        uint64_t tasks;
        while (true) {
            int ret = poll(&pfd, 1, timeout);
            if (ret != 0)
                break;
            externals.callAll();
        }
        int s = read(readerEventfd_, &tasks, sizeof(uint64_t));
        if (s != sizeof(uint64_t))
            continue;

        for (uint64_t i = 0; i < tasks; i++) {
            auto task = iPacMan_.getNextTask();
            while (!task->pack.data_.ptr_) {
                cslog() << "net: invalid packet processor!!!!!!!!!";
            }
            processTask(task);
        }
#endif
#if defined(WIN32) || defined(__APPLE__)
#ifdef WIN32
        while (true) {
            auto ret = WaitForSingleObject(readerEvent_, 50);  // timeout 50ms
            if (ret != WAIT_TIMEOUT)
                break;
            externals.callAll();
        };
#else
        while (true) {
            struct kevent event;
            int ret = kevent(readerKq_, NULL, 0, &event, 1, &timeout);
            if (ret)
                break;
            externals.callAll();
        }
#endif
        while (readerLock.test_and_set(std::memory_order_acquire))  // acquire lock
            ;                                                       // spin
        int tasks = readerTaskCount_;
        readerTaskCount_ = 0;
        readerLock.clear(std::memory_order_release);  // release lock

        for (int i = 0; i < tasks; i++) {
            auto task = iPacMan_.getNextTask();
            processTask(task);
        }
#endif
    }
    cswarning() << "processorRoutine STOPPED!!!\n";
}

inline void Network::processTask(TaskPtr<IPacMan>& task) {
    auto remoteSender = transport_->getPackSenderEntry(task->sender);

    if (!(task->pack.isHeaderValid())) {
        static constexpr size_t limit = 100;
        auto size = (task->pack.size() <= limit) ? task->pack.size() : limit;

        cswarning() << "Header is not valid: " << cs::Utils::byteStreamToHex(static_cast<const char*>(task->pack.data()), size);
        remoteSender->addStrike();
        return;
    }

    // Pure network processing, prior blacklist inspection to allow re-registration
    if (task->pack.isNetwork()) {
        if (cs::PacketValidator::instance().validate(task->pack)) {
            transport_->processNetworkTask(task, remoteSender);
        }
        return;
    }

    // test blacklist, the only way to remove from the list is to re-register again
    if (remoteSender->isBlackListed()) {
        csdebug() << "Message is ignored from blacklisted " << task->sender;
        return;
    }

    // Non-network data
    uint32_t& recCounter = packetMap_.tryStore(task->pack.getHash());
    if (!recCounter && task->pack.addressedToMe(transport_->getMyPublicKey())) {
        if (task->pack.isFragmented() || task->pack.isCompressed()) {
            bool newFragmentedMsg = false;
            MessagePtr msg = collector_.getMessage(task->pack, newFragmentedMsg);
            transport_->gotPacket(task->pack, remoteSender);

            if (newFragmentedMsg) {
                transport_->registerMessage(msg);
            }

            if (msg && msg->isComplete()) {
                if (cs::PacketValidator::instance().validate(**msg)) {
                    transport_->processNodeMessage(**msg);
                }
            }
        }
        else {
            if (cs::PacketValidator::instance().validate(task->pack)) {
                transport_->processNodeMessage(task->pack);
            }
        }
    }

    transport_->redirectPacket(task->pack, remoteSender);
    ++recCounter;
}

void Network::sendDirect(const Packet& p, const ip::udp::endpoint& ep) {
    auto qePtr = oPacMan_.allocNext();

    if (ep.size() > 16) {
        cslog() << "endpoint address too big " << ep.size();
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(ep.data());
        for (size_t i = 0; i < ep.size(); i++) {
            cslog() << *ptr++;
        }
    }
    qePtr->endpoint = ep;
    qePtr->pack = p;

    oPacMan_.enQueueLast();
#ifdef __linux__
    static uint64_t one = 1;
    write(writerEventfd_, &one, sizeof(uint64_t));
#endif
#if defined(WIN32) || defined(__APPLE__)
    while (writerLock.test_and_set(std::memory_order_acquire))  // acquire lock
        ;                                                       // spin
    writerTaskCount_.fetch_add(1, std::memory_order_relaxed);
#ifdef WIN32
    SetEvent(writerEvent_);
#else
    kevent(writerKq_, &writerEvent_, 1, NULL, 0, NULL);
#endif
    writerLock.clear(std::memory_order_release);  // release lock
#endif
}

Network::Network(const Config& config, Transport* transport)
: resolver_(context_)
, transport_(transport) {
#ifdef __linux__
    readerEventfd_ = eventfd(0, 0);
    if (readerEventfd_ == -1) {
        good_ = false;
        return;
    }

    writerEventfd_ = eventfd(0, 0);
    if (writerEventfd_ == -1) {
        good_ = false;
        return;
    }
#elif WIN32
    writerEvent_ = CreateEvent(nullptr,            // default security attributes
                               FALSE,              // automatic-reset event
                               FALSE,              // initial state is nonsignaled
                               TEXT("WriteEvent")  // object name
    );

    if (writerEvent_ == nullptr) {
        good_ = false;
        return;
    }

    readerEvent_ = CreateEvent(nullptr,           // default security attributes
                               FALSE,             // automatic-reset event
                               FALSE,             // initial state is nonsignaled
                               TEXT("ReadEvent")  // object name
    );

    if (writerEvent_ == nullptr) {
        good_ = false;
        return;
    }
#elif __APPLE__
    readerKq_ = kqueue();
    if (readerKq_ == -1) {
        good_ = false;
        return;
    }

    EV_SET(&readerEvent_, 0, EVFILT_USER, EV_ADD | EV_DISPATCH | EV_DISABLE, NOTE_FFCOPY | NOTE_TRIGGER, 0, NULL);
    int ret = kevent(readerKq_, &readerEvent_, 1, NULL, 0, NULL);

    if (ret == -1 || (readerEvent_.flags & EV_ERROR)) {
        good_ = false;
        return;
    }

    EV_SET(&readerEvent_, 0, EVFILT_USER, EV_DISPATCH | EV_ENABLE, NOTE_FFCOPY | NOTE_TRIGGER, 0, NULL);

    writerKq_ = kqueue();
    if (writerKq_ == -1) {
        good_ = false;
        return;
    }

    EV_SET(&writerEvent_, 0, EVFILT_USER, EV_ADD | EV_DISPATCH | EV_DISABLE, NOTE_FFCOPY | NOTE_TRIGGER, 0, NULL);
    ret = kevent(writerKq_, &writerEvent_, 1, NULL, 0, NULL);

    if (ret == -1 || (writerEvent_.flags & EV_ERROR)) {
        good_ = false;
        return;
    }

    EV_SET(&writerEvent_, 0, EVFILT_USER, EV_DISPATCH | EV_ENABLE, NOTE_FFCOPY | NOTE_TRIGGER, 0, NULL);
#endif
    readerThread_ = std::thread(&Network::readerRoutine, this, config);
    writerThread_ = std::thread(&Network::writerRoutine, this, config);
    processorThread_ = std::thread(&Network::processorRoutine, this);

    if (!config.hasTwoSockets()) {
        auto sockPtr = new ip::udp::socket(bindSocket(context_, this, config.getInputEndpoint(), config.useIPv6()));

        if (!sockPtr->is_open()) {
            good_ = false;
            return;
        }

        singleSock_.store(sockPtr);
        singleSockOpened_.store(true);
    }

    while (readerStatus_.load() == ThreadStatus::NonInit)
        ;
    while (writerStatus_.load() == ThreadStatus::NonInit)
        ;

    good_ = (readerStatus_.load() == ThreadStatus::Success && writerStatus_.load() == ThreadStatus::Success);

    if (!good_) {
        cserror() << "Cannot start the network: error binding sockets";
    }
}

bool Network::resendFragment(const cs::Hash& hash, const uint16_t id, const ip::udp::endpoint& ep) {
    MessagePtr msg;

    {
        cs::Lock lock(collector_.mLock_);
        msg = collector_.map_.tryStore(hash);
    }

    if (!msg) {
        return false;
    }

    {
        cs::Lock l(msg->pLock_);
        if (id < msg->packetsTotal_ && *(msg->packets_ + id)) {
            sendDirect(*(msg->packets_ + id), ep);
            return true;
        }
    }

    return false;
}

void Network::sendInit() {
    initFlag_.store(true);
}

void Network::registerMessage(Packet* pack, const uint32_t size) {
    if (size >= Packet::MaxFragments) {
        cserror() << "Too much fragments in message to send (" << size << "), ignore";
        return;
    }

    MessagePtr msg;

    {
        cs::Lock l(collector_.mLock_);
        msg = collector_.msgAllocator_.emplace();
    }

    msg->packetsLeft_ = 0;
    msg->packetsTotal_ = size;
    msg->headerHash_ = pack->getHeaderHash();

    auto packEnd = msg->packets_ + size;
    auto rPtr = pack;
    for (auto wPtr = msg->packets_; wPtr != packEnd; ++wPtr, ++rPtr) {
        *wPtr = *rPtr;
    }

    {
        cs::Lock l(collector_.mLock_);
        collector_.map_.tryStore(pack->getHeaderHash()) = msg;
    }
}

Network::~Network() {
    stopReaderRoutine = true;

    if (readerThread_.joinable()) {
        readerThread_.join();
    }

    stopWriterRoutine = true;

    if (writerThread_.joinable()) {
        writerThread_.join();
    }

    stopProcessorRoutine = true;

    if (processorThread_.joinable()) {
        processorThread_.join();
    }

    delete singleSock_.load();
}
