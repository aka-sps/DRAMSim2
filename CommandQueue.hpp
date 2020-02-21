/** @file
*  @copyright (c) 2010-2011, Elliott Cooper-Balis
*                             Paul Rosenfeld
*                             Bruce Jacob
*                             University of Maryland 
*                             dramninjas [at] gmail [dot] com
*  All rights reserved.
*  
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions are met:
*  
*     * Redistributions of source code must retain the above copyright notice,
*        this list of conditions and the following disclaimer.
*  
*     * Redistributions in binary form must reproduce the above copyright notice,
*        this list of conditions and the following disclaimer in the documentation
*        and/or other materials provided with the distribution.
*  
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
*  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
*  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
*  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
*  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
*  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
*  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
*  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
*  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*********************************************************************************/
#ifndef CMDQUEUE_HPP
#define CMDQUEUE_HPP

#include "BankState.hpp"
#include "Transaction.hpp"
#include "SimulatorObject.hpp"

#include <vector>

namespace DRAMSim {
class CommandQueue
    : public SimulatorObject
{
    CommandQueue(void);

    std::ostream &dramsim_log;

public:
    typedef std::vector<BusPacket *> BusPacket1D;
    typedef std::vector<BusPacket1D> BusPacket2D;
    typedef std::vector<BusPacket2D> BusPacket3D;

    CommandQueue(std::vector<std::vector<BankState> > &states,
                 std::ostream &dramsim_log);
    virtual
        ~CommandQueue(void);

    void
        enqueue(BusPacket *newBusPacket);
    bool
        pop(BusPacket **busPacket);
    bool
        hasRoomFor(unsigned numberToEnqueue,
                   unsigned rank,
                   unsigned bank);
    bool
        isIssuable(BusPacket *busPacket);
    bool
        isEmpty(unsigned rank);
    void
        needRefresh(unsigned rank);
    void
        print(void);
    void
        update(void);  ///< SimulatorObject requirement
    std::vector<BusPacket*> &
        getCommandQueue(unsigned rank,
                        unsigned bank);

    BusPacket3D queues;  ///< 3D array of BusPacket pointers
    std::vector<std::vector<BankState>> &bankStates;

private:
    void
        nextRankAndBank(unsigned &rank,
                        unsigned &bank);

private:
    unsigned nextBank;
    unsigned nextRank;

    unsigned nextBankPRE;
    unsigned nextRankPRE;

    unsigned refreshRank;
    bool refreshWaiting;

    std::vector<std::vector<unsigned>> tFAWCountdown;
    std::vector<std::vector<unsigned>> rowAccessCounters;

    bool sendAct;
};
}  // namespace DRAMSim

#endif  // CMDQUEUE_HPP

