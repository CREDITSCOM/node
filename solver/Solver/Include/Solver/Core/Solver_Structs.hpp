//
// Created by alexraag on 02.05.2018.
//

#pragma once
#include <stdint.h>
#include <string>
#include <vector>


using namespace std;

namespace Credits{
    const short MAX_STR = 256;

    struct Transc
    {
        uint64_t Hash;
        // uuid_t InnerID;

        uint32_t Amount;
        uint64_t Amount1;

        char A_source[MAX_STR];
        char A_target[MAX_STR];
        char Currency[MAX_STR];
    };


    struct ValidTransaction
    {
        std::string TransactionHash;
        uint64_t amount;
        uint64_t amount1;
    };

    typedef std::string node_id;




    using namespace std;

    const short MAX_STR_SIZE = 256;
    using d_vector = vector<double> ;

    struct Transaction
    {
        uint64_t Hash;
//        UUID InnerID;
        char A_source[MAX_STR_SIZE];
        char A_target[MAX_STR_SIZE];
        uint32_t Amount;
        uint64_t Amount1;
        char Currency[MAX_STR_SIZE];
    };

    struct Balance
    {
        char A_source[MAX_STR_SIZE];
        char Currency[MAX_STR_SIZE];
        uint32_t amount;
        uint64_t amount1;
    };


    class StrHashString
    {
    public:
        string transaction;
        uint32_t amount;
        uint64_t amount1;


        StrHashString(uint32_t _amount, uint64_t _amount1, string _transaction)
        {
            amount = _amount;
            amount1 = _amount1;
            transaction = _transaction;
        }
    };
}