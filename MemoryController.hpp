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
*/
#ifndef MEMORYCONTROLLER_HPP
#define MEMORYCONTROLLER_HPP

#include "CommandQueue.hpp"

#include <map>

namespace DRAMSim {

class MemorySystem;
class Rank;
class CSVWriter;
class Transaction;
class BusPacket;

class MemoryController
    : public SimulatorObject
{
public:
    MemoryController(MemorySystem* ms,
                     CSVWriter &csvOut_,
                     std::ostream &dramsim_log_);
    virtual
        ~MemoryController();

    bool
        addTransaction(Transaction *trans);
    bool
        WillAcceptTransaction(void);
    void
        returnReadData(const Transaction *trans);
    void
        receiveFromBus(BusPacket *bpacket);
    void
        attachRanks(std::vector<Rank*> *ranks);
    void
        update(void);
    void
        printStats(bool finalStats = false);
    void
        resetStats(void);

    std::vector<Transaction *> transactionQueue;

private:
    std::ostream &dramsim_log;
    std::vector<std::vector<BankState> > bankStates;
    void
        insertHistogram(unsigned latencyValue,
                        unsigned rank,
                        unsigned bank);

    MemorySystem *parentMemorySystem;
    CommandQueue commandQueue;
    BusPacket *poppedBusPacket;
    std::vector<unsigned>refreshCountdown;
    std::vector<BusPacket *> writeDataToSend;
    std::vector<unsigned> writeDataCountdown;
    std::vector<Transaction *> returnTransaction;
    std::vector<Transaction *> pendingReadTransactions;
    std::map<unsigned, unsigned> latencies;  ///< latencyValue -> latencyCount
    std::vector<bool> powerDown;
    std::vector<Rank *> *ranks;

    CSVWriter &csvOut;

    // these packets are counting down waiting to be transmitted on the "bus"
    BusPacket *outgoingCmdPacket;
    unsigned cmdCyclesLeft;
    BusPacket *outgoingDataPacket;
    unsigned dataCyclesLeft;

    uint64_t totalTransactions;
    std::vector<uint64_t> grandTotalBankAccesses;
    std::vector<uint64_t> totalReadsPerBank;
    std::vector<uint64_t> totalWritesPerBank;
    std::vector<uint64_t> totalReadsPerRank;
    std::vector<uint64_t> totalWritesPerRank;
    std::vector< uint64_t > totalEpochLatency;

    unsigned channelBitWidth;
    unsigned rankBitWidth;
    unsigned bankBitWidth;
    unsigned rowBitWidth;
    unsigned colBitWidth;
    unsigned byteOffsetWidth;
    unsigned refreshRank;

public:
    // energy values are per rank -- SST uses these directly, so make these public 
    std::vector<uint64_t> backgroundEnergy;
    std::vector<uint64_t> burstEnergy;
    std::vector<uint64_t> actpreEnergy;
    std::vector<uint64_t> refreshEnergy;
};
}  // namespace DRAMSim

#endif  // MEMORYCONTROLLER_HPP
