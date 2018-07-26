//
// Created by alexraag on 04.05.2018.
//

#pragma once

#include <string>

class Node;
namespace Credits{

    enum class solver_type{
        real,fake,test
    };

    class ISolver;
    class SolverFactory{
    public:
        SolverFactory();
        ~SolverFactory();

        SolverFactory(const SolverFactory&)= delete;
        SolverFactory& operator=(const SolverFactory&)= delete;


        ISolver* createSolver(solver_type type, Node*);
    };
}
