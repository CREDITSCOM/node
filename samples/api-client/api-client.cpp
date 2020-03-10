#include <API.h>

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>

#include <iostream>

struct Host
{
    std::string ip;
    uint16_t port{ 0 };
};

bool test(const Host& host)
{
    using namespace apache::thrift;
    using namespace apache::thrift::protocol;
    using namespace apache::thrift::transport;

    auto ptr = new TSocket(host.ip, host.port);
    ptr->setConnTimeout(10000);
    stdcxx::shared_ptr<TTransport> socket(ptr);
    stdcxx::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
    stdcxx::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
    api::APIClient client(protocol);

    bool ret = false;

    try {
        std::cout << "connecting to " << host.ip << ':' << host.port << std::endl;
        transport->open();

        std::cout << "make request to node API... ";
        api::SyncStateResult result;
        client.SyncStateGet(result);
        ret = (result.status.code == 0);
        std::cout << (ret ? "SUCCESS" : "FAILED") << std::endl;
        transport->close();
    }
    catch (TException & x) {
        std::cout << x.what() << std::endl;
    }
    return ret;
}

int main() {
    Host host{ "165.22.253.11", 9090 };
	return test(host);
}
