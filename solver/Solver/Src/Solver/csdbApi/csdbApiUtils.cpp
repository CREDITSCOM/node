//
// Created by diesonne on 07/05/2018.
//

#include "../../../Include/Solver/csdbApi/csdbApiUtils.hpp"

namespace Credits{
    csdbApiv2::csdbApiv2() {

    }

    csdbApiv2::~csdbApiv2() {

    }


    TransactionData csdbApiv2::getTransaction() {
        TransactionData data;
        convertTransaction(data);


        return data;
    }

    Wallet csdbApiv2::getWallet() {
        Wallet wallet;
        convertWallet(wallet);

        return wallet;
    }




    void csdbApiv2::convertTransaction(TransactionData& m_transaction) {

    }

    void csdbApiv2::convertWallet(Wallet& m_wallet) {

    }



    ApiFactory::ApiFactory() {
    }

    ApiFactory::~ApiFactory() {
    }


    IApi::Ptr ApiFactory::createApi(ApiFactory::ApiType type) {
        switch(type){
            case ApiType::csdb_v1:
                return nullptr;

            case ApiType::csdb_v2:
                return std::make_unique<csdbApiv2>();

            case ApiType::fake:
                return nullptr;
            case ApiType::test:
                return nullptr;

            default:
                return nullptr;
        }

        return nullptr;
    }

    IApi::IApi() {

    }

    IApi::~IApi() {

    }

    TransactionData IApi::getTransaction() {
        return TransactionData();
    }

    Wallet IApi::getWallet() {
        return Wallet();
    }
}