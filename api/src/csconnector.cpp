#include "csconnector/csconnector.h"
#include <csdb/currency.h>

namespace csconnector {

    using namespace stdcxx;

    csconnector::csconnector(BlockChain &m_blockchain, Credits::ISolver* solver, const Config &config)
      :  api_(make_shared<APIHandler>(m_blockchain, *solver)),
         server(make_shared<APIProcessor>(api_),
                    make_shared<TServerSocket>(config.port),
                    make_shared<TBufferedTransportFactory>(),
                    make_shared<TBinaryProtocolFactory>())

    {
        thread = std::thread([this, config]() {
            try {
                Log("csconnector started on port ", config.port);
                server.run();
            } catch (...) {
                std::cerr << "Oh no! I'm dead :'-(" << std::endl;
            }
        });
    }

    csconnector::~csconnector() {
        server.stop();

        if (thread.joinable()) {
            thread.join();
        }

        Log("csconnector stopped");
    }
}
