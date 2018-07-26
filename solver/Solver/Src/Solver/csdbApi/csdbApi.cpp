//
// Created by diesonne on 07/05/2018.
//

//#include <Solver/csdbApi/csdbApi.hpp>




#include "../../../Include/Solver/csdbApi/csdbApiUtils.hpp"
#include "../../../Include/Solver/csdbApi/csdbApi.hpp"

namespace Credits{

    csdbApi::csdbApi(ApiFactory::ApiType type)
            :factory()
            ,m_api(nullptr) {
        m_api = factory.createApi(type);
    }

    csdbApi::~csdbApi() {

    }

    Wallet csdbApi::getWallet() {
        Wallet wallet;

        if(m_api!=nullptr)
            wallet = m_api->getWallet();

        return wallet;
    }

    TransactionData csdbApi::getTransaction() {
        TransactionData data;
        if(m_api != nullptr)
            data = m_api->getTransaction();
        return data;
    }





}