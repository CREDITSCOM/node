#pragma once

#include "ProxiTypes.h"
#include "INodeState.h"
#include <memory>
#include "CallsQueueScheduler.h"

class StartState;
class NormalState;
class NormalSyncState;
class TrustedState;
class TrustedCollectState;
class TrustedWriteState;

//TODO: discuss possibility to switch states after timeout expired, timeouts can be individual but controlled by SolverCore

class SolverCore
{
public:
    
    SolverCore();

    ~SolverCore();

    void setStartState();
    void setNormalState();
    void setNormalSyncState();
    void setTrustedState();
    void setTrustedCollectState();
    void setTrustedWriteState();

    // do not call from within states!!!
    void onRecvBlock(const Block& block)
    {
        m_pState->onRecvBlock(*this, block);
    }

    // do not call from within states!!!
    void onRecvRoundTable(const RoundTable& table)
    {
        m_pState->onRecvRoundTable(*this, table);
    }

    // do not call from within states!!!
    void onRecvVector(const Vector& vector)
    {
        m_pState->onRecvVector(*this, vector);
    }

    // do not call from within states!!!
    void onRecvMatrix(const Matrix& matrix)
    {
        m_pState->onRecvMatrix(*this, matrix);
    }

    void Finish()
    {
        m_pState->stateOff(*this);
        m_scheduler.RemoveAll();
        m_stateExpiredTag = CallsQueueScheduler::no_tag;
        m_pState = &m_noState;
        m_shouldStop = true;
    }

    bool isFinished() const
    {
        return m_shouldStop;
    }

private:
    INodeState * m_pState;

    StartState * m_pStartState;
    NormalState * m_pNormalState;
    NormalSyncState * m_pNormalSyncState;
    TrustedState * m_pTrustedState;
    TrustedCollectState * m_pTrustedCollectState;
    TrustedWriteState * m_pTrustedWriteState;

    void setState(INodeState * pState);

    constexpr static uint32_t DefaultStateTimeout = 5000;
    CallsQueueScheduler m_scheduler;
    CallsQueueScheduler::CallTag m_stateExpiredTag;

    bool m_shouldStop;

    class NoneState : public INodeState
    {
        void onRecvBlock(SolverCore& context, const Block& block) {}

        void onRecvRoundTable(SolverCore& context, const RoundTable& table) {}

        void onRecvVector(SolverCore& context, const Vector& vector) {}

        void onRecvMatrix(SolverCore& context, const Matrix& matrix) {}

        const char * getName() const override
        {
            return "None";
        }
    };

    NoneState m_noState;
};
