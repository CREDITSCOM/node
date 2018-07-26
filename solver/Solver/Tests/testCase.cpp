//
// Created by alexraag on 08.05.2018.
//

#include "testCase.hpp"
#include <memory>

namespace Tests{




    fakeNode::fakeNode(node_type type)
            :factory()
            ,solver(nullptr)
            ,_type(type)
             {

    }

    fakeNode::~fakeNode() {

    }

    void fakeNode::setup() {
        if(solver == nullptr){
            //TODO test hash
            solver = factory.createSolver(solver_type::real,"");
        }
    }


    Transport::Transport()
            :fakenodes()
            ,isValid(true)
    { setup(); }

    Transport::~Transport() {

    }

    void Transport::Run() {
        while(isValid){

        }
    }

    void Transport::setup() {
        fakeNode::Ptr Node1
                = std::make_unique<fakeNode>(node_type::main);
        fakeNode::Ptr Node2
                = std::make_unique<fakeNode>(node_type::confidant);
        fakeNode::Ptr Node3
                = std::make_unique<fakeNode>(node_type::confidant);
        fakeNode::Ptr Node4
                = std::make_unique<fakeNode>(node_type::confidant);
        fakeNode::Ptr Node5
                = std::make_unique<fakeNode>(node_type::normal);

        fakenodes.push_back(Node1);
        fakenodes.push_back(Node2);
        fakenodes.push_back(Node3);
        fakenodes.push_back(Node4);
        fakenodes.push_back(Node5);

    }
}