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
#include "MemoryController.hpp"

#include "SystemConfiguration.hpp"

#include <cassert>

namespace DRAMSim {
using namespace std;

CommandQueue::CommandQueue(std::vector<std::vector<BankState> > &states,
                           std::ostream &dramsim_log_)
    : dramsim_log(dramsim_log_)
    , bankStates(states)
    , rowAccessCounters(NUM_RANKS, vector<unsigned>(NUM_BANKS, 0))
{
    // use numBankQueues below to create queue structure
    size_t numBankQueues;

    if (queuingStructure == PerRank) {
        numBankQueues = 1;
    } else if (queuingStructure == PerRankPerBank) {
        numBankQueues = NUM_BANKS;
    } else {
        ERROR("== Error - Unknown queuing structure");
        throw std::logic_error("== Error - Unknown queuing structure");
    }

    // create queue based on the structure we want
    BusPacket2D perBankQueue;
    this->queues = BusPacket3D();

    /// @todo using resize
    for (size_t rank = 0; rank < NUM_RANKS; ++rank) {
        // this loop will run only once for per-rank and NUM_BANKS times for per-rank-per-bank
        for (size_t bank = 0; bank < numBankQueues; ++bank) {
            perBankQueue.push_back(BusPacket1D());
        }

        queues.push_back(perBankQueue);
    }

    // FOUR-bank activation window
    //   this will count the number of activations within a given window
    //   (decrementing counter)
    // 
    // countdown vector will have decrementing counters starting at tFAW
    //   when the 0th element reaches 0, remove it
    tFAWCountdown.reserve(NUM_RANKS);

    for (size_t i = 0; i < NUM_RANKS; ++i) {
        // init the empty vectors here so we don't seg fault later
        tFAWCountdown.push_back(vector<unsigned>());
    }
}

CommandQueue::~CommandQueue(void)
{
    // ERROR("COMMAND QUEUE destructor");
    size_t const bankMax = queuingStructure == PerRank ? 1 : NUM_RANKS;

    for (size_t r = 0; r < NUM_RANKS; ++r) {
        for (size_t b = 0; b < bankMax; ++b) {
            auto &qrb = queues[r][b];

            for (size_t i = 0; i < qrb.size(); ++i) {
                delete qrb[i];
            }

            qrb.clear();
        }
    }
}

/// Adds a command to appropriate queue
void
CommandQueue::enqueue(BusPacket *const newBusPacket)
{
    auto const rank = newBusPacket->rank;
    auto &q_r = this->queues[rank];

    if (queuingStructure == PerRank) {
        q_r[0].push_back(newBusPacket);

        if (q_r[0].size() > CMD_QUEUE_DEPTH) {
            ERROR("== Error - Enqueued more than allowed in command queue  Need to call .hasRoomFor(int numberToEnqueue, unsigned rank, unsigned bank) first");
            throw std::logic_error("== Error - Unknown queuing structure");
        }
    } else if (queuingStructure == PerRankPerBank) {
        auto const bank = newBusPacket->bank;
        auto &q_r_b = q_r[bank];
        q_r_b.push_back(newBusPacket);

        if (q_r_b.size() > CMD_QUEUE_DEPTH) {
            ERROR("== Error - Enqueued more than allowed in command queue Need to call .hasRoomFor(int numberToEnqueue, unsigned rank, unsigned bank) first");
            throw std::logic_error("Enqueued more than allowed in command queue");
        }
    } else {
        ERROR("== Error - Unknown queuing structure");
        throw std::logic_error("Unknown queuing structure");
    }
}

/// Removes the next item from the command queue based on the system's command scheduling policy
bool
CommandQueue::pop(BusPacket **const busPacket)
{
    // this can be done here because pop() is called every clock cycle by the parent MemoryController
    //   figures out the sliding window requirement for tFAW
    // 
    // deal with tFAW book-keeping
    //   each rank has it's own counter since the restriction is on a device level
    for (size_t i = 0; i < NUM_RANKS; ++i) {
        // decrement all the counters we have going
        for (auto &el: tFAWCountdown[i]) {
            --el;
        }

        // the head will always be the smallest counter, so check if it has reached 0
        if (tFAWCountdown[i].size() > 0 && tFAWCountdown[i][0] == 0) {
            tFAWCountdown[i].erase(tFAWCountdown[i].begin());
        }
    }

    /* Now we need to find a packet to issue. When the code picks a packet, it will set
             *busPacket = [some eligible packet]

             First the code looks if any refreshes need to go
             Then it looks for data packets
             Otherwise, it starts looking for rows to close (in open page)
    */

    if (rowBufferPolicy == ClosePage) {
        bool sendingREF = false;

        // if the memory controller set the flags signaling that we need to issue a refresh
        if (refreshWaiting) {
            bool foundActiveOrTooEarly = false;

            // look for an open bank
            for (auto b = decltype(NUM_BANKS)(0); b < NUM_BANKS; ++b) {
                auto &queue = getCommandQueue(refreshRank, b);

                // checks to make sure that all banks are idle
                if (bankStates[refreshRank][b].currentBankState == BankState::RowActive) {
                    foundActiveOrTooEarly = true;

                    // if the bank is open, make sure there is nothing else
                    //  going there before we close it
                    for (size_t j = 0; j < queue.size(); j++) {
                        auto const packet = queue[j];

                        if (packet->row == bankStates[refreshRank][b].openRowAddress && packet->bank == b) {
                            if (packet->busPacketType != ACTIVATE && isIssuable(packet)) {
                                *busPacket = packet;
                                queue.erase(queue.begin() + j);
                                sendingREF = true;
                            }

                            break;
                        }
                    }

                    break;
                } else if (bankStates[refreshRank][b].nextActivate > currentClockCycle) {
                    /// @note checks nextActivate time for each bank to make sure tRP is being
                    ///              satisfied.  the next ACT and next REF can be issued at the same
                    ///              point in the future, so just use nextActivate field instead of
                    ///              creating a nextRefresh field
                    foundActiveOrTooEarly = true;
                    break;
                }
            }

            // if there are no open banks and timing has been met, send out the refresh
            //   reset flags and rank pointer
            if (!foundActiveOrTooEarly && bankStates[refreshRank][0].currentBankState != BankState::PowerDown) {
                *busPacket = new BusPacket(REFRESH, 0, 0, 0, refreshRank, 0, 0, dramsim_log);
                refreshRank = -1;
                refreshWaiting = false;
                sendingREF = true;
            }
        }  // if refreshWaiting

        // if we're not sending a REF, proceed as normal
        if (!sendingREF) {
            bool foundIssuable = false;
            unsigned const startingRank = this->nextRank;
            unsigned const startingBank = this->nextBank;

            for (;;) {
                auto &queue = getCommandQueue(nextRank, nextBank);

                // make sure there is something in this queue first
                //  also make sure a rank isn't waiting for a refresh
                //  if a rank is waiting for a refresh, don't issue anything to it until the
                //      refresh logic above has sent one out (ie, letting banks close)
                if (!queue.empty() && !((nextRank == refreshRank) && refreshWaiting)) {
                    if (queuingStructure == PerRank) {
                        // search from beginning to find first issuable bus packet
                        for (size_t i = 0; i < queue.size(); i++) {
                            if (isIssuable(queue[i])) {
                                // check to make sure we aren't removing a read/write that is paired with an activate
                                if (i > 0 && queue[i - 1]->busPacketType == ACTIVATE && queue[i - 1]->physicalAddress == queue[i]->physicalAddress) {
                                    continue;
                                }

                                *busPacket = queue[i];
                                queue.erase(queue.begin() + i);
                                foundIssuable = true;
                                break;
                            }
                        }
                    } else {
                        if (this->isIssuable(queue[0])) {
                            // no need to search because if the front can't be sent,
                            //  then no chance something behind it can go instead
                            *busPacket = queue[0];
                            queue.erase(queue.begin());
                            foundIssuable = true;
                        }
                    }
                }

                // if we found something, break out of do-while
                if (foundIssuable) {
                    break;
                }

                // rank round robin
                if (queuingStructure == PerRank) {
                    this->nextRank = (this->nextRank + 1) % NUM_RANKS;

                    if (startingRank == this->nextRank) {
                        break;
                    }
                } else {
                    nextRankAndBank(this->nextRank, this->nextBank);

                    if (startingRank == this->nextRank && startingBank == this->nextBank) {
                        break;
                    }
                }
            }

            // if we couldn't find anything to send, return false
            if (!foundIssuable) {
                return false;
            }
        }
    } else if (rowBufferPolicy == OpenPage) {
        bool sendingREForPRE = false;

        if (this->refreshWaiting) {
            bool sendREF = true;

            // make sure all banks idle and timing met for a REF
            for (auto b = decltype(NUM_BANKS)(0); b < NUM_BANKS; ++b) {
                // if a bank is active we can't send a REF yet
                if (bankStates[refreshRank][b].currentBankState == BankState::RowActive) {
                    sendREF = false;
                    bool closeRow = true;
                    // search for commands going to an open row
                    auto &refreshQueue = getCommandQueue(refreshRank, b);

                    for (size_t j = 0; j < refreshQueue.size(); ++j) {
                        auto const packet = refreshQueue[j];

                        // if a command in the queue is going to the same row . . .
                        if (!(bankStates[refreshRank][b].openRowAddress == packet->row && b == packet->bank)) {
                            continue;
                        }

                        // . . . and is not an activate . . .
                        if (packet->busPacketType != ACTIVATE) {
                            closeRow = false;

                            // . . . and can be issued . . .
                            if (isIssuable(packet)) {
                                // send it out
                                *busPacket = packet;
                                refreshQueue.erase(refreshQueue.begin() + j);
                                sendingREForPRE = true;
                            }
                        }

                        break;
                    }

                    // if the bank is open and we are allowed to close it, then send a PRE
                    if (closeRow && currentClockCycle >= bankStates[refreshRank][b].nextPrecharge) {
                        rowAccessCounters[refreshRank][b] = 0;
                        *busPacket = new BusPacket(PRECHARGE, 0, 0, 0, refreshRank, b, 0, dramsim_log);
                        sendingREForPRE = true;
                    }

                    break;
                } else if (bankStates[refreshRank][b].nextActivate > currentClockCycle) {
                    /// @note the next ACT and next REF can be issued at the same
                    ///              point in the future, so just use nextActivate field instead of
                    ///              creating a nextRefresh field
                    /// and this bank doesn't have an open row
                    sendREF = false;
                    break;
                }
            }

            if (sendREF && bankStates[refreshRank][0].currentBankState != BankState::PowerDown) {
                // if there are no open banks and timing has been met, send out the refresh
                //   reset flags and rank pointer
                *busPacket = new BusPacket(REFRESH, 0, 0, 0, refreshRank, 0, 0, dramsim_log);
                refreshRank = -1;
                refreshWaiting = false;
                sendingREForPRE = true;
            }
        }

        if (!sendingREForPRE) {
            unsigned startingRank = nextRank;
            unsigned startingBank = nextBank;
            bool foundIssuable = false;

            for (;;) {
                // round robin over queues
                auto &queue = getCommandQueue(this->nextRank, this->nextBank);

                // make sure there is something there first
                if (!queue.empty() && !(this->nextRank == this->refreshRank && this->refreshWaiting)) {
                    // search from the beginning to find first issuable bus packet
                    for (size_t i = 0; i < queue.size(); i++) {
                        auto const packet = queue[i];

                        if (isIssuable(packet)) {
                            // check for dependencies
                            bool dependencyFound = false;

                            for (size_t j = 0; j < i; j++) {
                                auto const prevPacket = queue[j];

                                if (prevPacket->busPacketType != ACTIVATE &&
                                        prevPacket->bank == packet->bank &&
                                        prevPacket->row == packet->row) {
                                    dependencyFound = true;
                                    break;
                                }
                            }

                            if (dependencyFound) {
                                continue;
                            }

                            *busPacket = packet;

                            // if the bus packet before is an activate, that is the act that was
                            //   paired with the column access we are removing, so we have to remove
                            //   that activate as well (check i>0 because if i==0 then there is nothing before it)
                            if (i > 0 && queue[i - 1]->busPacketType == ACTIVATE) {
                                rowAccessCounters[(*busPacket)->rank][(*busPacket)->bank]++;
                                // i is being returned, but i-1 is being thrown away, so must delete it here
                                delete (queue[i - 1]);

                                // remove both i-1 (the activate) and i (we've saved the pointer in *busPacket)
                                queue.erase(queue.begin() + i - 1, queue.begin() + i + 1);
                            } else { // there's no activate before this packet
                                // or just remove the one bus packet
                                queue.erase(queue.begin() + i);
                            }

                            foundIssuable = true;
                            break;
                        }
                    }
                }

                // if we found something, break out of do-while
                if (foundIssuable) {
                    break;
                }

                // rank round robin
                if (queuingStructure == PerRank) {
                    nextRank = (nextRank + 1) % NUM_RANKS;

                    if (startingRank == nextRank) {
                        break;
                    }
                } else {
                    nextRankAndBank(nextRank, nextBank);

                    if (startingRank == nextRank && startingBank == nextBank) {
                        break;
                    }
                }
            }

            // if nothing was issuable, see if we can issue a PRE to an open bank
            //   that has no other commands waiting
            if (!foundIssuable) {
                // search for banks to close
                bool sendingPRE = false;
                unsigned startingRank = nextRankPRE;
                unsigned startingBank = nextBankPRE;

                do { // round robin over all ranks and banks
                    vector <BusPacket *> &queue = getCommandQueue(nextRankPRE, nextBankPRE);
                    bool found = false;

                    // check if bank is open
                    if (bankStates[nextRankPRE][nextBankPRE].currentBankState == BankState::RowActive) {
                        for (size_t i = 0; i < queue.size(); i++) {
                            // if there is something going to that bank and row, then we don't want to send a PRE
                            if (queue[i]->bank == nextBankPRE &&
                                    queue[i]->row == bankStates[nextRankPRE][nextBankPRE].openRowAddress) {
                                found = true;
                                break;
                            }
                        }

                        // if nothing found going to that bank and row or too many accesses have happened, close it
                        if (!found || rowAccessCounters[nextRankPRE][nextBankPRE] == TOTAL_ROW_ACCESSES) {
                            if (currentClockCycle >= bankStates[nextRankPRE][nextBankPRE].nextPrecharge) {
                                sendingPRE = true;
                                rowAccessCounters[nextRankPRE][nextBankPRE] = 0;
                                *busPacket = new BusPacket(PRECHARGE, 0, 0, 0, nextRankPRE, nextBankPRE, 0, dramsim_log);
                                break;
                            }
                        }
                    }

                    nextRankAndBank(nextRankPRE, nextBankPRE);
                } while (!(startingRank == nextRankPRE && startingBank == nextBankPRE));

                // if no PREs could be sent, just return false
                if (!sendingPRE) {
                    return false;
                }
            }
        }
    }

    // sendAct is flag used for posted-cas
    //  posted-cas is enabled when AL>0
    //  when sendAct is true, when don't want to increment our indexes
    //  so we send the column access that is paid with this act
    if (AL > 0 && sendAct) {
        sendAct = false;
    } else {
        sendAct = true;
        nextRankAndBank(nextRank, nextBank);
    }

    // if its an activate, add a tfaw counter
    if ((*busPacket)->busPacketType == ACTIVATE) {
        tFAWCountdown[(*busPacket)->rank].push_back(tFAW);
    }

    return true;
}

/// check if a rank/bank queue has room for a certain number of bus packets
bool
CommandQueue::hasRoomFor(unsigned const numberToEnqueue,
                         unsigned const rank,
                         unsigned const bank)
{
    auto const &queue = getCommandQueue(rank, bank);
    return CMD_QUEUE_DEPTH - queue.size() >= numberToEnqueue;
}

/// prints the contents of the command queue
void
CommandQueue::print(void)
{
    if (queuingStructure == PerRank) {
        PRINT(endl << "== Printing Per Rank Queue");

        for (size_t i = 0; i < NUM_RANKS; ++i) {
            PRINT(" = Rank " << i << "  size : " << queues[i][0].size());

            for (size_t j = 0; j < queues[i][0].size(); ++j) {
                PRINTN("    " << j << "]");
                queues[i][0][j]->print();
            }
        }

        return;
    }
    
    if (queuingStructure == PerRankPerBank) {
        PRINT("\n== Printing Per Rank, Per Bank Queue");

        for (size_t i = 0; i < NUM_RANKS; ++i) {
            PRINT(" = Rank " << i);

            for (size_t j = 0; j < NUM_BANKS; ++j) {
                PRINT("    Bank " << j << "   size : " << queues[i][j].size());

                for (size_t k = 0; k < queues[i][j].size(); ++k) {
                    PRINTN("       " << k << "]");
                    queues[i][j][k]->print();
                }
            }
        }
    }
}

/**
 * @return a reference to the queue for a given rank, bank. Since we
 * don't always have a per bank queuing structure, sometimes the bank
 * argument is ignored (and the 0th index is returned
 */
vector<BusPacket*>&
CommandQueue::getCommandQueue(unsigned const rank,
                              unsigned const bank)
{
    auto &qr = queues[rank];

    if (queuingStructure == PerRankPerBank) {
        return qr[bank];
    }
    
    if (queuingStructure == PerRank) {
        return qr[0];
    }

    throw std::logic_error("Unknown queue structure");
}

/// checks if busPacket is allowed to be issued
bool
CommandQueue::isIssuable(BusPacket * busPacket)
{
    switch (busPacket->busPacketType) {
    case REFRESH:
        break;

    case ACTIVATE:
        return
            (bankStates[busPacket->rank][busPacket->bank].currentBankState == BankState::Idle ||
             bankStates[busPacket->rank][busPacket->bank].currentBankState == BankState::Refreshing) &&
            currentClockCycle >= bankStates[busPacket->rank][busPacket->bank].nextActivate &&
            tFAWCountdown[busPacket->rank].size() < 4;

    case WRITE:
    case WRITE_P:
        return
            bankStates[busPacket->rank][busPacket->bank].currentBankState == BankState::RowActive &&
            currentClockCycle >= bankStates[busPacket->rank][busPacket->bank].nextWrite &&
            busPacket->row == bankStates[busPacket->rank][busPacket->bank].openRowAddress &&
            rowAccessCounters[busPacket->rank][busPacket->bank] < TOTAL_ROW_ACCESSES;

    case READ_P:
    case READ:
        return
            bankStates[busPacket->rank][busPacket->bank].currentBankState == BankState::RowActive &&
            currentClockCycle >= bankStates[busPacket->rank][busPacket->bank].nextRead &&
            busPacket->row == bankStates[busPacket->rank][busPacket->bank].openRowAddress &&
            rowAccessCounters[busPacket->rank][busPacket->bank] < TOTAL_ROW_ACCESSES;

    case PRECHARGE:
        return
            bankStates[busPacket->rank][busPacket->bank].currentBankState == BankState::RowActive &&
            currentClockCycle >= bankStates[busPacket->rank][busPacket->bank].nextPrecharge;

    default:
        ERROR("== Error - Trying to issue a crazy bus packet type : ");
        busPacket->print();
        throw std::logic_error("Trying to issue a crazy bus packet type");
    }

    return false;
}

/// figures out if a rank's queue is empty
bool
CommandQueue::isEmpty(unsigned const rank)
{
    auto const &q_rank = this->queues[rank];

    if (queuingStructure == PerRank) {
        return q_rank[0].empty();
    }
    
    if (queuingStructure != PerRankPerBank) {
        throw std::logic_error("Invalid Queueing Structure");
    }

    /// @todo std::any/std::all
    for (size_t i = 0; i < NUM_BANKS; ++i) {
        if (!q_rank[i].empty()) {
            return false;
        }
    }

    return true;
}

/// tells the command queue that a particular rank is in need of a refresh
void
CommandQueue::needRefresh(unsigned const rank)
{
    this->refreshWaiting = true;
    this->refreshRank = rank;
}

void
CommandQueue::nextRankAndBank(unsigned &rank,
                              unsigned &bank)
{
    if (schedulingPolicy == RankThenBankRoundRobin) {
        if (++rank == NUM_RANKS) {
            rank = 0;

            if (++bank == NUM_BANKS) {
                bank = 0;
            }
        }

        return;
    }
    
    if (schedulingPolicy == BankThenRankRoundRobin) {
        // bank-then-rank round robin
        if (++bank == NUM_BANKS) {
            bank = 0;

            if (++rank == NUM_RANKS) {
                rank = 0;
            }
        }

        return;
    }

    ERROR("== Error - Unknown scheduling policy");
    throw std::logic_error("Unknown scheduling policy");
}

void
CommandQueue::update(void)
{
    /// do nothing since pop() is effectively update(),
    /// needed for SimulatorObject
    /// @todo make CommandQueue not a SimulatorObject
}

}  // namespace DRAMSim
