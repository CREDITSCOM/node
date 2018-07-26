//
// Created by alexraag on 08.05.2018.
//



//#include <Solver/Fake/Fake_Generals.hpp>


#include <iostream>
#include <sstream>
#include <string.h>

#include "../../../Include/Solver/csdbApi/csdbApiUtils.hpp"
#include "../../../Include/Solver/Fake/Fake_Generals.hpp"
//#include "../../../Include/Solver/Fake/Fake_Solver.hpp"

#include <algorithm>
#include <csdb/currency.h>
#include <csdb/address.h>
#include <csdb/pool.h>
#include <csdb/transaction.h>

#include <mutex>


namespace Credits{

    Fake_Generals::Fake_Generals() { }
    Fake_Generals::~Fake_Generals() { }

    std::string Fake_Generals::buildvector(const csdb::Transaction& _transaction) {
        std::string result;
        result.clear();

        std::vector<int64_t> deltas;

		int64_t delta = _transaction.balance().integral() - _transaction.amount().integral();
        deltas.push_back(delta);

        encrypt_vector(result, deltas);
        vector_datas.emplace_back(result);

        return result;
    }

    void Fake_Generals::addvector(std::string vector) {
        vector.append("$");
        vector_datas.emplace_back(vector);
    }

    void Fake_Generals::addmatrix(std::string matrix) {
        //matrix_data.emplace_back(matrix);
    }

    int Fake_Generals::take_desision(const std::vector<PublicKey>& confidantNodes, const PublicKey& myId) {
        const int write_id = 2;
        //decode_matrix(matrix_data[0]);

		if (myId != confidantNodes[write_id]) return 0;
        return 2;
    }

    void Fake_Generals::encrypt_vector(std::string& vector_string
            ,std::vector<int64_t>& vector_data) {
        for (auto& it:vector_data)
            vector_string += (std::to_string(it) + "*");
    }

    int Fake_Generals::decode_matrix(std::string& matrix) {
        return 0;
    }

    std::string Fake_Generals::getMatrix() {
        std::string result = "";
        for(auto& it:vector_datas){
            result.append(it);
        }
        return result;
    }

	void Fake_Generals::chooseHeadAndTrusted(std::map<string, string>) { }
    void Fake_Generals::chooseHeadAndTrustedFake(std::vector<std::string>& hashes) { }
    void Fake_Generals::fake_block(std::string m_public_key) { }
}
