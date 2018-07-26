//
// Created by alexraag on 03.05.2018.
//
#include <iostream>
#include <Solver/Solver.hpp>
#include <Solver/SolverFactory.hpp>

#include <memory>

using namespace Credits;

int main(){

    SolverFactory factory;
    std::unique_ptr<ISolver> solver;
    solver = factory.createSolver(solver_type::real,"");


    try {
        solver->send_fake_transcation();
    }
    catch (const std::exception& ex){
        std::cout<<ex.what()<<std::endl;
    }


    return 0;
}