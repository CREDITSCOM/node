#ifndef STDAFX_H
#define STDAFX_H

#include <cassert>
#include <mutex>

#include <boost/io/ios_state.hpp>

#include <csdb/address.hpp>
#include <csdb/amount.hpp>
#include <csdb/amount_commission.hpp>
#include <csdb/currency.hpp>
#include <csdb/pool.hpp>
#include <csdb/storage.hpp>
#include <csdb/transaction.hpp>

#include <thrift/protocol/TJSONProtocol.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/THttpServer.h>
#include <thrift/transport/TBufferTransports.h>

#include <iomanip>
#include <scope_guard.h>
#include <solver/solvercore.hpp>
#include <ContractExecutor.h>

#endif //STDAFX_H
