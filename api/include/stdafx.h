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

#if defined(_MSC_VER)
#pragma warning(push)
// 4245: 'return': conversion from 'int' to 'SOCKET', signed/unsigned mismatch
#pragma warning(disable: 4245)
#include <thrift/protocol/TJSONProtocol.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/THttpServer.h>
#include <thrift/transport/TBufferTransports.h>
#pragma warning(pop)
#endif // _MSC_VER

#include <iomanip>
#include <scope_guard.h>
#include <solver/solvercore.hpp>
#include <ContractExecutor.h>

#endif //STDAFX_H
