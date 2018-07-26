//
// Created by alexraag on 04.05.2018.
//

//#include <Solver/SolverFactory.hpp>

#include <memory>

#include "../../Include/Solver/Fake/Fake_Solver.hpp"
#include "../../Include/Solver/SolverFactory.hpp"

#include <csnode/node.hpp>


namespace Credits{

    SolverFactory::SolverFactory() {
    }
    SolverFactory::~SolverFactory() {
    }

    ISolver* SolverFactory::createSolver(solver_type type, Node* node) {
        switch (type){
            case solver_type::real:

                return nullptr;
            case solver_type::fake:
                return new Fake_Solver(node);
            case solver_type::test:
                return nullptr;
            default:
                return nullptr;
        }
    }
}
