//
// Created by alexraag on 08.05.2018.
//

#pragma once

#include <vector>
#include <string>

#include "../csdbApi/csdbApiUtils.hpp"
#include <csdb/csdb.h>
#include <csdb/pool.h>
#include <map>

#include <csnode/node.hpp>

namespace Credits{

	class Fake_Solver;
    class Fake_Generals{
    public:
        Fake_Generals();
        ~Fake_Generals();

        Fake_Generals(const Fake_Generals&)= delete;
        Fake_Generals& operator=(const Fake_Generals&)= delete;

        //Rewrite method//
        void chooseHeadAndTrusted(std::map<std::string, std::string>);
        void chooseHeadAndTrustedFake(std::vector<std::string>& hashes);

        std::string buildvector(const csdb::Transaction&);

        void addvector(std::string vector);
        void addmatrix(std::string matrix);

        //take desision
        int take_desision(const std::vector<PublicKey>&, const PublicKey&);
        std::string getMatrix();

        void fake_block(std::string);

    private:
        void encrypt_vector(std::string& vector_string
                ,std::vector<int64_t>& vector_data);

        int decode_matrix(std::string& matrix);

        std::vector<TransactionData> transaction_data;

        std::vector<std::string> vector_datas;
        std::vector<std::string> matrix_data;
    };
}
