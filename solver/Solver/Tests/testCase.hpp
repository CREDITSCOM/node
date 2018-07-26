//
// Created by alexraag on 08.05.2018.
//

#pragma once

#include <Solver/SolverFactory.hpp>
#include <memory>
#include <list>

using namespace Credits;


namespace Tests{


    enum class node_type {
        normal,confidant,main,write};


    class fakeNode{
    public:
        using Ptr = std::unique_ptr<fakeNode>;
    public:
        fakeNode(node_type);
        ~fakeNode();

        fakeNode(const fakeNode&)= delete;
        fakeNode& operator=(const fakeNode&)= delete;


    private:
        void setup();

    private:
        std::unique_ptr<ISolver> solver;
        SolverFactory factory;
        node_type _type;
    };

    class Transport {
    public:
        Transport();
        ~Transport();

        Transport(const Transport&)= delete;
        Transport& operator=(const Transport&)= delete;


        void Run();
    private:
        void setup();
    private:
        std::list<fakeNode::Ptr> fakenodes;
        bool isValid;
    };
}





