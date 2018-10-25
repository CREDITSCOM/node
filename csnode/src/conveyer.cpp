#include "csnode/conveyer.h"

struct cs::Conveyer::Impl
{

};

cs::Conveyer::Conveyer()
{

}

cs::Conveyer::~Conveyer()
{

}

const cs::Conveyer& cs::Conveyer::instance() const
{
    static cs::Conveyer conveyer;
    return conveyer;
}
