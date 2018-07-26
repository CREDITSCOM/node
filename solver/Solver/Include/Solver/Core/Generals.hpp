//
// Created by alexraag on 01.05.2018.
//

#pragma once


#include <memory>

#include "Solver_Util.hpp"
#include "Solver_Core.hpp"
#include "strhash.hpp"
#include "Solver_Structs.hpp"


#include "../../../Include/Solver/csdbApi/csdbApi.hpp"

namespace Credits{

    const short min_hashes_size = 4;
    constexpr short min_nodes = 4;
    //Класс для работы с алгоритмом Генералов
    class Generals{
    public:
        explicit Generals(std::string);
        ~Generals();

        Generals(const Generals&)= delete;
        Generals& operator=(const Generals&)= delete;

        //Normal Method
        void chooseHeadAndTrusted(std::multimap<string, string>);

        //Need to Rewrite Method
        void CheckTransactionPool(std::vector<std::string> transaction_pool);

        int FormPool(std::string);


        /*
        *@brief Принятие решения для блока транзакций потранзакционно, подготовка блока
        */
        int TakeDecision();



        //Util//
        void VectorToString(string& outputString);
        void FinalVectorToStr(string& outputString);


        /*
        *@brief Cшивка векторов в таблицу в виде типа string
        */
        void BuildStrMap(std::string strVerifiedPool);



        std::string& getVector();


        //depricated//
        /*
        *@brief Получение таблиц в виде типа string и сшивка их в таблицу для принятия решения
        */
        //    void GetTableOfVerifiedPools(std::map<std::string, std::vector<ValidTransaction> >& table);

    private:
        bool Init();
        void writeToBase(const std::string&);

    private:
        bool startTableOfVerifiedPools;

        std::unique_ptr<StrHashTransaction> str_hash_trans;
        std::unique_ptr<Services> services;


        std::map<std::string
                ,std::vector<ValidTransaction> > table_of_verified_pools;
        std::map<std::string, std::map<node_id
                , std::vector<ValidTransaction> > > pool_for_decision;


        vector<ValidTransaction> verified_pool;
        vector<string*> transanctions4record;

        string tableOfVerifiedPools;
        string node_hash;
        string cell_ID;


        vector <node_id> trusted_members;
        vector <string> tr4record;
        vector <string> list_transactions_core;
        vector<std::string> transaction_pool;


        uint32_t n_trans, n_trans_limit;
        Transc buf_trans;

        csdbApi m_csdbApi;
    };
}
