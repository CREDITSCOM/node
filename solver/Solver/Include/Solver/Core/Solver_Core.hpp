//
// Created by alexraag on 01.05.2018.
//

#pragma once

namespace Credits{
    class Solver_Core{
    public:
        Solver_Core();
        ~Solver_Core();

        Solver_Core(const Solver_Core&)= delete;
        Solver_Core& operator=(const Solver_Core&)= delete;

        void validate_transaction();
    private:
    };
}