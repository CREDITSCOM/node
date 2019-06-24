#include <apicompat.hpp>
#include <thrift/TToString.h>

#include <optional>

namespace api
{
    namespace compat
    {
        std::optional<api::SmartContractInvocation> read(const std::string& src) {
            api::SmartContractInvocation sci;
            if (try_read_v0(src, sci)) {
                return std::move(sci);
            }
            return std::nullopt;
        }

        void copy(const v0::SmartContractInvocation& from, api::SmartContractInvocation& to) {

        }

        bool try_read_v0(/*[in]*/ const std::string& src, /*[out]*/ api::SmartContractInvocation& dest) {
            std::string tmp_src(src);
            v0::SmartContractInvocation tmp_dest;
            try {
                tmp_dest = ::deserialize<v0::SmartContractInvocation>(std::move(tmp_src));
            }
            catch (...) {
                return false;
            }
            copy(tmp_dest, dest);
            return true;
        }

        namespace v0
        {
            SmartContractInvocation::~SmartContractInvocation() throw() {
            }

            void SmartContractInvocation::__set_sourceCode(const std::string& val) {
                this->sourceCode = val;
            }

            void SmartContractInvocation::__set_byteCode(const std::string& val) {
                this->byteCode = val;
            }

            void SmartContractInvocation::__set_hashState(const std::string& val) {
                this->hashState = val;
            }

            void SmartContractInvocation::__set_method(const std::string& val) {
                this->method = val;
            }

            void SmartContractInvocation::__set_params(const std::vector< ::variant::Variant>& val) {
                this->params = val;
            }

            void SmartContractInvocation::__set_forgetNewState(const bool val) {
                this->forgetNewState = val;
            }
            std::ostream& operator<<(std::ostream& out, const SmartContractInvocation& obj)
            {
                obj.printTo(out);
                return out;
            }


            uint32_t SmartContractInvocation::read(::apache::thrift::protocol::TProtocol* iprot) {

                ::apache::thrift::protocol::TInputRecursionTracker tracker(*iprot);
                uint32_t xfer = 0;
                std::string fname;
                ::apache::thrift::protocol::TType ftype;
                int16_t fid;

                xfer += iprot->readStructBegin(fname);

                using ::apache::thrift::protocol::TProtocolException;


                while (true)
                {
                    xfer += iprot->readFieldBegin(fname, ftype, fid);
                    if (ftype == ::apache::thrift::protocol::T_STOP) {
                        break;
                    }
                    switch (fid)
                    {
                    case 1:
                        if (ftype == ::apache::thrift::protocol::T_STRING) {
                            xfer += iprot->readString(this->sourceCode);
                            this->__isset.sourceCode = true;
                        }
                        else {
                            xfer += iprot->skip(ftype);
                        }
                        break;
                    case 2:
                        if (ftype == ::apache::thrift::protocol::T_STRING) {
                            xfer += iprot->readBinary(this->byteCode);
                            this->__isset.byteCode = true;
                        }
                        else {
                            xfer += iprot->skip(ftype);
                        }
                        break;
                    case 3:
                        if (ftype == ::apache::thrift::protocol::T_STRING) {
                            xfer += iprot->readString(this->hashState);
                            this->__isset.hashState = true;
                        }
                        else {
                            xfer += iprot->skip(ftype);
                        }
                        break;
                    case 4:
                        if (ftype == ::apache::thrift::protocol::T_STRING) {
                            xfer += iprot->readString(this->method);
                            this->__isset.method = true;
                        }
                        else {
                            xfer += iprot->skip(ftype);
                        }
                        break;
                    case 5:
                        if (ftype == ::apache::thrift::protocol::T_LIST) {
                            {
                                this->params.clear();
                                uint32_t _size16;
                                ::apache::thrift::protocol::TType _etype19;
                                xfer += iprot->readListBegin(_etype19, _size16);
                                this->params.resize(_size16);
                                uint32_t _i20;
                                for (_i20 = 0; _i20 < _size16; ++_i20)
                                {
                                    xfer += this->params[_i20].read(iprot);
                                }
                                xfer += iprot->readListEnd();
                            }
                            this->__isset.params = true;
                        }
                        else {
                            xfer += iprot->skip(ftype);
                        }
                        break;
                    case 6:
                        if (ftype == ::apache::thrift::protocol::T_BOOL) {
                            xfer += iprot->readBool(this->forgetNewState);
                            this->__isset.forgetNewState = true;
                        }
                        else {
                            xfer += iprot->skip(ftype);
                        }
                        break;
                    default:
                        xfer += iprot->skip(ftype);
                        break;
                    }
                    xfer += iprot->readFieldEnd();
                }

                xfer += iprot->readStructEnd();

                return xfer;
            }

            uint32_t SmartContractInvocation::write(::apache::thrift::protocol::TProtocol* oprot) const {
                uint32_t xfer = 0;
                ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
                xfer += oprot->writeStructBegin("SmartContractInvocation");

                xfer += oprot->writeFieldBegin("sourceCode", ::apache::thrift::protocol::T_STRING, 1);
                xfer += oprot->writeString(this->sourceCode);
                xfer += oprot->writeFieldEnd();

                xfer += oprot->writeFieldBegin("byteCode", ::apache::thrift::protocol::T_STRING, 2);
                xfer += oprot->writeBinary(this->byteCode);
                xfer += oprot->writeFieldEnd();

                xfer += oprot->writeFieldBegin("hashState", ::apache::thrift::protocol::T_STRING, 3);
                xfer += oprot->writeString(this->hashState);
                xfer += oprot->writeFieldEnd();

                xfer += oprot->writeFieldBegin("method", ::apache::thrift::protocol::T_STRING, 4);
                xfer += oprot->writeString(this->method);
                xfer += oprot->writeFieldEnd();

                xfer += oprot->writeFieldBegin("params", ::apache::thrift::protocol::T_LIST, 5);
                {
                    xfer += oprot->writeListBegin(::apache::thrift::protocol::T_STRUCT, static_cast<uint32_t>(this->params.size()));
                    std::vector< ::variant::Variant> ::const_iterator _iter21;
                    for (_iter21 = this->params.begin(); _iter21 != this->params.end(); ++_iter21)
                    {
                        xfer += (*_iter21).write(oprot);
                    }
                    xfer += oprot->writeListEnd();
                }
                xfer += oprot->writeFieldEnd();

                xfer += oprot->writeFieldBegin("forgetNewState", ::apache::thrift::protocol::T_BOOL, 6);
                xfer += oprot->writeBool(this->forgetNewState);
                xfer += oprot->writeFieldEnd();

                xfer += oprot->writeFieldStop();
                xfer += oprot->writeStructEnd();
                return xfer;
            }

            void swap(SmartContractInvocation& a, SmartContractInvocation& b) {
                using ::std::swap;
                swap(a.sourceCode, b.sourceCode);
                swap(a.byteCode, b.byteCode);
                swap(a.hashState, b.hashState);
                swap(a.method, b.method);
                swap(a.params, b.params);
                swap(a.forgetNewState, b.forgetNewState);
                swap(a.__isset, b.__isset);
            }

            SmartContractInvocation::SmartContractInvocation(const SmartContractInvocation& other22) {
                sourceCode = other22.sourceCode;
                byteCode = other22.byteCode;
                hashState = other22.hashState;
                method = other22.method;
                params = other22.params;
                forgetNewState = other22.forgetNewState;
                __isset = other22.__isset;
            }
            SmartContractInvocation::SmartContractInvocation(SmartContractInvocation&& other23) {
                sourceCode = std::move(other23.sourceCode);
                byteCode = std::move(other23.byteCode);
                hashState = std::move(other23.hashState);
                method = std::move(other23.method);
                params = std::move(other23.params);
                forgetNewState = std::move(other23.forgetNewState);
                __isset = std::move(other23.__isset);
            }
            SmartContractInvocation& SmartContractInvocation::operator=(const SmartContractInvocation& other24) {
                sourceCode = other24.sourceCode;
                byteCode = other24.byteCode;
                hashState = other24.hashState;
                method = other24.method;
                params = other24.params;
                forgetNewState = other24.forgetNewState;
                __isset = other24.__isset;
                return *this;
            }
            SmartContractInvocation& SmartContractInvocation::operator=(SmartContractInvocation&& other25) {
                sourceCode = std::move(other25.sourceCode);
                byteCode = std::move(other25.byteCode);
                hashState = std::move(other25.hashState);
                method = std::move(other25.method);
                params = std::move(other25.params);
                forgetNewState = std::move(other25.forgetNewState);
                __isset = std::move(other25.__isset);
                return *this;
            }
            void SmartContractInvocation::printTo(std::ostream& out) const {
                using ::apache::thrift::to_string;
                out << "SmartContractInvocation(";
                out << "sourceCode=" << to_string(sourceCode);
                out << ", " << "byteCode=" << to_string(byteCode);
                out << ", " << "hashState=" << to_string(hashState);
                out << ", " << "method=" << to_string(method);
                out << ", " << "params=" << to_string(params);
                out << ", " << "forgetNewState=" << to_string(forgetNewState);
                out << ")";
            }

        }
    } // api::compat
} // api
