
#ifndef __STDAFX_H__
#define __STDAFX_H__

#include <stack>
#include <algorithm>
#include <cassert>
#include <type_traits>
#include <functional>
#include <mutex>

#include <boost/io/ios_state.hpp>

#include <csdb/address.h>
#include <csdb/amount.h>
#include <csdb/amount_commission.h>
#include <csdb/currency.h>
#include <csdb/pool.h>
#include <csdb/storage.h>
#include <csdb/transaction.h>

#include <thrift/transport/TSocket.h>
#include <thrift/protocol/TJSONProtocol.h>
#include <thrift/transport/THttpServer.h>

#include <iomanip>
#include <scope_guard.h>
#include <solver2/SolverCore.h>
#include <ContractExecutor.h>

#endif //__STDAFX_H__