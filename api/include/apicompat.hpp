#pragma once

#include <apihandler.hpp>
#include "compat/v0/variant_types.h"

#include <thrift/Thrift.h>
#include <thrift/TApplicationException.h>
#include <thrift/TBase.h>
#include <thrift/protocol/TProtocol.h>
#include <thrift/transport/TTransport.h>
#include <thrift/stdcxx.h>

#include <optional>

namespace api
{
    namespace compat
    {
        std::optional<api::SmartContractInvocation> read(const std::string& src);

        // commit 0bbd1452d28464738aa32db0f2d1a3f89e3c1178
        // date 01.11.2018 16:02
        namespace v0
        {
            typedef struct _SmartContractInvocation__isset {
                _SmartContractInvocation__isset() : sourceCode(false), byteCode(false), hashState(false), method(false), params(false), forgetNewState(false) {}
                bool sourceCode : 1;
                bool byteCode : 1;
                bool hashState : 1;
                bool method : 1;
                bool params : 1;
                bool forgetNewState : 1;
            } _SmartContractInvocation__isset;

            class SmartContractInvocation : public virtual ::apache::thrift::TBase {
            public:

                SmartContractInvocation(const SmartContractInvocation&);
                SmartContractInvocation(SmartContractInvocation&&);
                SmartContractInvocation& operator=(const SmartContractInvocation&);
                SmartContractInvocation& operator=(SmartContractInvocation&&);
                SmartContractInvocation() : sourceCode(), byteCode(), hashState(), method(), forgetNewState(0) {
                }

                virtual ~SmartContractInvocation() throw();
                std::string sourceCode;
                std::string byteCode;
                std::string hashState;
                std::string method;
                std::vector< ::variant::Variant>  params;
                bool forgetNewState;

                _SmartContractInvocation__isset __isset;

                void __set_sourceCode(const std::string& val);

                void __set_byteCode(const std::string& val);

                void __set_hashState(const std::string& val);

                void __set_method(const std::string& val);

                void __set_params(const std::vector< ::variant::Variant>& val);

                void __set_forgetNewState(const bool val);

                bool operator == (const SmartContractInvocation& rhs) const
                {
                    if (!(sourceCode == rhs.sourceCode))
                        return false;
                    if (!(byteCode == rhs.byteCode))
                        return false;
                    if (!(hashState == rhs.hashState))
                        return false;
                    if (!(method == rhs.method))
                        return false;
                    if (!(params == rhs.params))
                        return false;
                    if (!(forgetNewState == rhs.forgetNewState))
                        return false;
                    return true;
                }
                bool operator != (const SmartContractInvocation& rhs) const {
                    return !(*this == rhs);
                }

                bool operator < (const SmartContractInvocation&) const;

                uint32_t read(::apache::thrift::protocol::TProtocol* iprot);
                uint32_t write(::apache::thrift::protocol::TProtocol* oprot) const;

                virtual void printTo(std::ostream& out) const;
            };

            void swap(SmartContractInvocation& a, SmartContractInvocation& b);

            std::ostream& operator<<(std::ostream& out, const SmartContractInvocation& obj);

        }

        void copy(const v0::SmartContractInvocation& from, api::SmartContractInvocation& to);
        bool try_read_v0(/*[in]*/ const std::string& src, /*[out]*/ api::SmartContractInvocation& dest);

    } // api::compat
} // api
