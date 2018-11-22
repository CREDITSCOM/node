#ifndef STDAFX_H
#define STDAFX_H

#include <cassert>
#include <mutex>

#include <boost/io/ios_state.hpp>

#include <csdb/address.h>
#include <csdb/amount.h>
#include <csdb/amount_commission.h>
#include <csdb/currency.h>
#include <csdb/pool.h>
#include <csdb/storage.h>
#include <csdb/transaction.h>

#include <thrift/protocol/TJSONProtocol.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/THttpServer.h>
#include <thrift/transport/TBufferTransports.h>

#include <iomanip>
#include <scope_guard.h>
#include <solver2/solvercore.hpp>
#include <ContractExecutor.h>

#endif //STDAFX_H
