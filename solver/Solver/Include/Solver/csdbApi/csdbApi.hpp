//
// Created by diesonne on 06/05/2018.
//

#pragma once

#include "csdbApiUtils.hpp"

namespace Credits{

    class csdbApi{
    public:
        csdbApi(ApiFactory::ApiType);
        virtual ~csdbApi();


        csdbApi(const csdbApi&)= delete;
        csdbApi& operator=(const csdbApi&)= delete;


        void createTransaction();
        void createWallet();

        TransactionData getTransaction();
        Wallet getWallet();
    protected:
        ApiFactory factory;
        IApi::Ptr m_api;
    };



}
