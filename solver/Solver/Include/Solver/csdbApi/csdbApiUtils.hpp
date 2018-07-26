//
// Created by diesonne on 07/05/2018.
//

#pragma once
#include <string>
#include <cstdint>

#include <memory>

//#if defined(__unix__)
//#include <sys/dtrace.h>
//#endif

//#if defined(WIN32)
//    #include <Rpc.h>
//#endif


using namespace std;
namespace Credits{

    struct TransactionData{
        string from, to;

        uint32_t amount;
        uint64_t amount1;

        //balance
        uint32_t balance;
        uint32_t balance1;
    };

    struct Wallet {
        long long balance;
        void createEmpty();
    };



    class IApi{
    public:
        using Ptr = std::unique_ptr<IApi>;
    public:
        IApi();
        virtual ~IApi();

        virtual TransactionData getTransaction();
        virtual Wallet getWallet();
    };
    class ApiFactory{
    public:
        enum class ApiType{csdb_v1,csdb_v2
                                ,fake,test};
    public:
        ApiFactory();
        ~ApiFactory();

        ApiFactory(const ApiFactory&)= delete;
        ApiFactory& operator=(const ApiFactory&)= delete;

        IApi::Ptr createApi(ApiType);
    };

    class csdbApiv2:public IApi{
    public:
        csdbApiv2();
        ~csdbApiv2();

        TransactionData getTransaction() override;
        Wallet getWallet() override;

    private:
        void convertTransaction(TransactionData&);
        void convertWallet(Wallet&);

    private:
      //  csdb::Transaction transaction;
      //  csdb::Wallet wallet;
    };
}
