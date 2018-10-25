#ifndef CONVEYER_H
#define CONVEYER_H

#include <memory>

class Node;

namespace cs
{
    class Solver;
}

namespace cs
{
    ///
    /// @brief The Conveyer class, represents utils and mechanics
    /// to transfer packets of transactions, consensus helper
    ///
    class Conveyer
    {
    private:
        Conveyer();
        ~Conveyer();

    public:

        ///
        /// @brief Instance of conveyer, singleton
        /// @return Returns static conveyer object reference, Meyers singleton
        ///
        const Conveyer& instance() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl;
    };
}

#endif // CONVEYER_H
