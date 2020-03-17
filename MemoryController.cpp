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
// Class file for memory controller object

#include "MemorySystem.hpp"
#include "AddressMapping.hpp"

#include <algorithm>

#define SEQUENTIAL(rank,bank) (rank * NUM_BANKS + bank)

namespace DRAMSim {

using namespace std;

MemoryController::MemoryController(MemorySystem *parent,
                                   CSVWriter &csvOut_,
                                   ostream &dramsim_log_)
    : parentMemorySystem(parent)
    , dramsim_log(dramsim_log_)
    , bankStates(NUM_RANKS, vector<BankState>(NUM_BANKS, dramsim_log))
    , commandQueue(bankStates, dramsim_log_)
    , csvOut(csvOut_)
    , powerDown(NUM_RANKS, false)
    , grandTotalBankAccesses(NUM_RANKS * NUM_BANKS, 0)
    , totalReadsPerBank(NUM_RANKS * NUM_BANKS, 0)
    , totalWritesPerBank(NUM_RANKS * NUM_BANKS, 0)
    , totalReadsPerRank(NUM_RANKS, 0)
    , totalWritesPerRank(NUM_RANKS, 0)
    , totalEpochLatency(NUM_RANKS * NUM_BANKS, 0)
    , backgroundEnergy(NUM_RANKS, 0)
    , burstEnergy(NUM_RANKS, 0)
    , actpreEnergy(NUM_RANKS, 0)
    , refreshEnergy(NUM_RANKS, 0)
{
    // reserve memory for vectors
    transactionQueue.reserve(TRANS_QUEUE_DEPTH);
    writeDataCountdown.reserve(NUM_RANKS);
    writeDataToSend.reserve(NUM_RANKS);
    refreshCountdown.reserve(NUM_RANKS);

    // staggers when each rank is due for a refresh
    for (size_t i = 0; i < NUM_RANKS; ++i) {
        refreshCountdown.push_back(int((REFRESH_PERIOD / tCK) / NUM_RANKS) * (i + 1));
    }
}

/// get a bus packet from either data or cmd bus
void
MemoryController::receiveFromBus(BusPacket *bpacket)
{
    if (bpacket->busPacketType != DATA) {
        ERROR("== Error - Memory Controller received a non-DATA bus packet from rank");
        bpacket->print();
        throw std::logic_error("Memory Controller received a non-DATA bus packet from rank");
    }

    if (DEBUG_BUS) {
        PRINTN(" -- MC Receiving From Data Bus : ");
        bpacket->print();
    }

    // add to return read data queue
    returnTransaction.push_back(new Transaction(RETURN_DATA, bpacket->physicalAddress, bpacket->data));
    totalReadsPerBank[SEQUENTIAL(bpacket->rank, bpacket->bank)]++;

    // this delete statement saves a mind-boggling amount of memory
    delete bpacket;
}

/// sends read data back to the CPU
void
MemoryController::returnReadData(const Transaction *trans)
{
    if (parentMemorySystem->ReturnReadData) {
        (*parentMemorySystem->ReturnReadData)(parentMemorySystem->systemID, trans->address, currentClockCycle);
    }
}

/// gives the memory controller a handle on the rank objects
void
MemoryController::attachRanks(vector<Rank *> *ranks)
{
    this->ranks = ranks;
}

/// memory controller update
void
MemoryController::update()
{
    // update bank states
    for (size_t i = 0; i < NUM_RANKS; ++i) {
        for (size_t j = 0; j < NUM_BANKS; ++j) {
            auto &bsij = bankStates[i][j];

            if (!(0 < bsij.stateChangeCountdown)) {
                continue;
            }

            // decrement counters
            --bsij.stateChangeCountdown;

            // if counter has reached 0, change state
            if (bsij.stateChangeCountdown != 0) {
                continue;
            }

            switch (bsij.lastCommand) {
                // only these commands have an implicit state change
            case WRITE_P:
            case READ_P:
                bsij.currentBankState = BankState::Precharging;
                bsij.lastCommand = PRECHARGE;
                bsij.stateChangeCountdown = tRP;
                break;

            case REFRESH:
            case PRECHARGE:
                bsij.currentBankState = BankState::Idle;
                break;

            default:
                break;
            }
        }
    }

    // check for outgoing command packets and handle countdowns
    if (this->outgoingCmdPacket) {
        --cmdCyclesLeft;

        if (cmdCyclesLeft == 0) {
            // packet is ready to be received by rank
            (*ranks)[outgoingCmdPacket->rank]->receiveFromBus(outgoingCmdPacket);
            outgoingCmdPacket = nullptr;
        }
    }

    // check for outgoing data packets and handle countdowns
    if (outgoingDataPacket) {
        --dataCyclesLeft;
        if (dataCyclesLeft == 0) {
            // inform upper levels that a write is done
            if (parentMemorySystem->WriteDataDone) {
                (*parentMemorySystem->WriteDataDone)(parentMemorySystem->systemID, outgoingDataPacket->physicalAddress, currentClockCycle);
            }

            (*ranks)[outgoingDataPacket->rank]->receiveFromBus(outgoingDataPacket);
            outgoingDataPacket = nullptr;
        }
    }


    // if any outstanding write data needs to be sent
    // and the appropriate amount of time has passed (WL)
    // then send data on bus
    // write data held in fifo vector along with countdowns
    if (writeDataCountdown.size() > 0) {
        for (auto &el: writeDataCountdown) {
            --el;
        }

        if (writeDataCountdown[0] == 0) {
            // send to bus and print debug stuff
            if (DEBUG_BUS) {
                PRINTN(" -- MC Issuing On Data Bus    : ");
                writeDataToSend[0]->print();
            }

            // queue up the packet to be sent
            if (outgoingDataPacket != nullptr) {
                ERROR("== Error - Data Bus Collision");
                throw std::logic_error("Data Bus Collision");
            }

            outgoingDataPacket = writeDataToSend[0];
            dataCyclesLeft = BL / 2;

            ++totalTransactions;
            totalWritesPerBank[SEQUENTIAL(writeDataToSend[0]->rank, writeDataToSend[0]->bank)]++;

            writeDataCountdown.erase(writeDataCountdown.begin());
            writeDataToSend.erase(writeDataToSend.begin());
        }
    }

    // if its time for a refresh issue a refresh
    //  else pop from command queue if it's not empty
    if (refreshCountdown[refreshRank] == 0) {
        commandQueue.needRefresh(refreshRank);
        (*ranks)[refreshRank]->refreshWaiting = true;
        refreshCountdown[refreshRank] = REFRESH_PERIOD / tCK;

        if (++refreshRank == NUM_RANKS) {
            refreshRank = 0;
        }
    } else if (powerDown[refreshRank] && refreshCountdown[refreshRank] <= tXP) {
        // if a rank is powered down, make sure we power it up in time for a refresh
        (*ranks)[refreshRank]->refreshWaiting = true;
    }

    // pass a pointer to a poppedBusPacket

    // function returns true if there is something valid in poppedBusPacket
    if (commandQueue.pop(&poppedBusPacket)) {
        if (poppedBusPacket->busPacketType == WRITE || poppedBusPacket->busPacketType == WRITE_P) {
            writeDataToSend.push_back(new BusPacket(DATA, poppedBusPacket->physicalAddress, poppedBusPacket->column,
                                      poppedBusPacket->row, poppedBusPacket->rank, poppedBusPacket->bank,
                                      poppedBusPacket->data, dramsim_log));
            writeDataCountdown.push_back(WL);
        }

        // update each bank's state based on the command that was just popped out of the command queue
        // for readability's sake
        unsigned const rank = poppedBusPacket->rank;
        unsigned const bank = poppedBusPacket->bank;

        switch (poppedBusPacket->busPacketType) {
        case READ_P:
        case READ:
            // add energy to account for total
            if (DEBUG_POWER) {
                PRINT(" ++ Adding Read energy to total energy");
            }
            burstEnergy[rank] += (IDD4R - IDD3N) * BL / 2 * NUM_DEVICES;
            if (poppedBusPacket->busPacketType == READ_P) {
                // Don't bother setting next read or write times because the bank is no longer active
                // bankStates[rank][bank].currentBankState = Idle;
                bankStates[rank][bank].nextActivate =
                    (std::max)(currentClockCycle + READ_AUTOPRE_DELAY, bankStates[rank][bank].nextActivate);
                bankStates[rank][bank].lastCommand = READ_P;
                bankStates[rank][bank].stateChangeCountdown = READ_TO_PRE_DELAY;
            } else if (poppedBusPacket->busPacketType == READ) {
                bankStates[rank][bank].nextPrecharge =
                    (std::max)(currentClockCycle + READ_TO_PRE_DELAY, bankStates[rank][bank].nextPrecharge);
                bankStates[rank][bank].lastCommand = READ;
            }

            for (size_t i = 0; i < NUM_RANKS; ++i) {
                for (size_t j = 0; j < NUM_BANKS; ++j) {
                    auto &bsij = bankStates[i][j];

                    if (i != poppedBusPacket->rank) {
                        // check to make sure it is active before trying to set (save's time?)
                        if (bsij.currentBankState == BankState::RowActive) {
                            bsij.nextRead = (std::max)(currentClockCycle + BL / 2 + tRTRS, bsij.nextRead);
                            bsij.nextWrite = (std::max)(currentClockCycle + READ_TO_WRITE_DELAY, bsij.nextWrite);
                        }
                    } else {
                        bsij.nextRead = (std::max)(currentClockCycle + (std::max)(tCCD, BL / 2), bsij.nextRead);
                        bsij.nextWrite = (std::max)(currentClockCycle + READ_TO_WRITE_DELAY, bsij.nextWrite);
                    }
                }
            }

            if (poppedBusPacket->busPacketType == READ_P) {
                // set read and write to nextActivate so the state table will prevent a read or write
                //  being issued (in cq.isIssuable())before the bank state has been changed because of the
                //  auto-precharge associated with this command
                bankStates[rank][bank].nextRead = bankStates[rank][bank].nextActivate;
                bankStates[rank][bank].nextWrite = bankStates[rank][bank].nextActivate;
            }

            break;
        case WRITE_P:
        case WRITE:
            if (poppedBusPacket->busPacketType == WRITE_P) {
                bankStates[rank][bank].nextActivate = (std::max)(currentClockCycle + WRITE_AUTOPRE_DELAY,
                                bankStates[rank][bank].nextActivate);
                bankStates[rank][bank].lastCommand = WRITE_P;
                bankStates[rank][bank].stateChangeCountdown = WRITE_TO_PRE_DELAY;
            } else if (poppedBusPacket->busPacketType == WRITE) {
                bankStates[rank][bank].nextPrecharge = (std::max)(currentClockCycle + WRITE_TO_PRE_DELAY,
                                bankStates[rank][bank].nextPrecharge);
                bankStates[rank][bank].lastCommand = WRITE;
            }


            // add energy to account for total
            if (DEBUG_POWER) {
                PRINT(" ++ Adding Write energy to total energy");
            }
            burstEnergy[rank] += (IDD4W - IDD3N) * BL / 2 * NUM_DEVICES;

            for (size_t i = 0; i < NUM_RANKS; ++i) {
                for (size_t j = 0; j < NUM_BANKS; ++j) {
                    auto &bsij = bankStates[i][j];

                    if (i != poppedBusPacket->rank) {
                        if (bsij.currentBankState == BankState::RowActive) {
                            bsij.nextWrite = (std::max)(currentClockCycle + BL / 2 + tRTRS, bsij.nextWrite);
                            bsij.nextRead = (std::max)(currentClockCycle + WRITE_TO_READ_DELAY_R, bsij.nextRead);
                        }
                    } else {
                        bsij.nextWrite = (std::max)(currentClockCycle + (std::max)(BL / 2, tCCD), bsij.nextWrite);
                        bsij.nextRead = (std::max)(currentClockCycle + WRITE_TO_READ_DELAY_B, bsij.nextRead);
                    }
                }
            }

            // set read and write to nextActivate so the state table will prevent a read or write
            //  being issued (in cq.isIssuable())before the bank state has been changed because of the
            //  auto-precharge associated with this command
            if (poppedBusPacket->busPacketType == WRITE_P) {
                bankStates[rank][bank].nextRead = bankStates[rank][bank].nextActivate;
                bankStates[rank][bank].nextWrite = bankStates[rank][bank].nextActivate;
            }

            break;
        case ACTIVATE:
            // add energy to account for total
            if (DEBUG_POWER) {
                PRINT(" ++ Adding Activate and Precharge energy to total energy");
            }

            actpreEnergy[rank] += ((IDD0 * tRC) - ((IDD3N * tRAS) + (IDD2N * (tRC - tRAS)))) * NUM_DEVICES;

            bankStates[rank][bank].currentBankState = BankState::RowActive;
            bankStates[rank][bank].lastCommand = ACTIVATE;
            bankStates[rank][bank].openRowAddress = poppedBusPacket->row;
            bankStates[rank][bank].nextActivate = (std::max)(currentClockCycle + tRC, bankStates[rank][bank].nextActivate);
            bankStates[rank][bank].nextPrecharge = (std::max)(currentClockCycle + tRAS, bankStates[rank][bank].nextPrecharge);

            // if we are using posted-CAS, the next column access can be sooner than normal operation

            bankStates[rank][bank].nextRead = (std::max)(currentClockCycle + (tRCD - AL), bankStates[rank][bank].nextRead);
            bankStates[rank][bank].nextWrite = (std::max)(currentClockCycle + (tRCD - AL), bankStates[rank][bank].nextWrite);

            for (size_t i = 0; i < NUM_BANKS; i++) {
                if (i != poppedBusPacket->bank) {
                    auto &bri = bankStates[rank][i];
                    bri.nextActivate = (std::max)(currentClockCycle + tRRD, bri.nextActivate);
                }
            }

            break;
        case PRECHARGE:
            bankStates[rank][bank].currentBankState = BankState::Precharging;
            bankStates[rank][bank].lastCommand = PRECHARGE;
            bankStates[rank][bank].stateChangeCountdown = tRP;
            bankStates[rank][bank].nextActivate = (std::max)(currentClockCycle + tRP, bankStates[rank][bank].nextActivate);

            break;
        case REFRESH:
            // add energy to account for total
            if (DEBUG_POWER) {
                PRINT(" ++ Adding Refresh energy to total energy");
            }

            refreshEnergy[rank] += (IDD5 - IDD3N) * tRFC * NUM_DEVICES;

            for (size_t i = 0; i < NUM_BANKS; ++i) {
                auto &bri = bankStates[rank][i];
                bri.nextActivate = currentClockCycle + tRFC;
                bri.currentBankState = BankState::Refreshing;
                bri.lastCommand = REFRESH;
                bri.stateChangeCountdown = tRFC;
            }

            break;
        default:
            ERROR("== Error - Popped a command we shouldn't have of type : " << poppedBusPacket->busPacketType);
            throw std::logic_error("Popped a command we shouldn't have of type");;
        }

        // issue on bus and print debug
        if (DEBUG_BUS) {
            PRINTN(" -- MC Issuing On Command Bus : ");
            poppedBusPacket->print();
        }

        // check for collision on bus
        if (outgoingCmdPacket != nullptr) {
            ERROR("== Error - Command Bus Collision");
            throw std::logic_error("Command Bus Collision");
        }

        outgoingCmdPacket = poppedBusPacket;
        cmdCyclesLeft = tCMD;

    }

    for (size_t i = 0; i < transactionQueue.size(); i++) {
        // pop off top transaction from queue
        //	assuming simple scheduling at the moment
        //	will eventually add policies here
        auto const transaction = transactionQueue[i];

        // map address to rank,bank,row,col
        unsigned newTransactionChan;
        unsigned newTransactionRank;
        unsigned newTransactionBank;
        unsigned newTransactionRow;
        unsigned newTransactionColumn;

        // pass these in as references so they get set by the addressMapping function
        addressMapping(transaction->address, newTransactionChan, newTransactionRank, newTransactionBank, newTransactionRow, newTransactionColumn);

        // if we have room, break up the transaction into the appropriate commands
        // and add them to the command queue
        if (commandQueue.hasRoomFor(2, newTransactionRank, newTransactionBank)) {
            if (DEBUG_ADDR_MAP) {
                PRINTN("== New Transaction - Mapping Address [0x" << hex << transaction->address << dec << "]");
                if (transaction->transactionType == DATA_READ) {
                    PRINT(" (Read)");
                } else {
                    PRINT(" (Write)");
                }
                PRINT("  Rank : " << newTransactionRank);
                PRINT("  Bank : " << newTransactionBank);
                PRINT("  Row  : " << newTransactionRow);
                PRINT("  Col  : " << newTransactionColumn);
            }



            // now that we know there is room in the command queue, we can remove from the transaction queue
            transactionQueue.erase(transactionQueue.begin() + i);

            // create activate command to the row we just translated
            auto const ACTcommand =
                new BusPacket(ACTIVATE, transaction->address,
                            newTransactionColumn, newTransactionRow, newTransactionRank,
                            newTransactionBank, 0, dramsim_log);

            // create read or write command and enqueue it
            auto const bpType = transaction->getBusPacketType();
            auto const command =
                new BusPacket(bpType, transaction->address,
                            newTransactionColumn, newTransactionRow, newTransactionRank,
                            newTransactionBank, transaction->data, dramsim_log);

            commandQueue.enqueue(ACTcommand);
            commandQueue.enqueue(command);

            // If we have a read, save the transaction so when the data comes back
            // in a bus packet, we can staple it back into a transaction and return it
            if (transaction->transactionType == DATA_READ) {
                pendingReadTransactions.push_back(transaction);
            } else {
                // just delete the transaction now that it's a buspacket
                delete transaction;
            }
            /* only allow one transaction to be scheduled per cycle -- this should
             * be a reasonable assumption considering how much logic would be
             * required to schedule multiple entries per cycle (parallel data
             * lines, switching logic, decision logic)
             */
            break;
        } else {
            // no room, do nothing this cycle
            // PRINT( "== Warning - No room in command queue" << endl;
        }
    }

    // calculate power
    //  this is done on a per-rank basis, since power characterization is done per device (not per bank)
    for (size_t i = 0; i < NUM_RANKS; i++) {
        if (USE_LOW_POWER) {
            // if there are no commands in the queue and that particular rank is not waiting for a refresh...
            if (commandQueue.isEmpty(i) && !(*ranks)[i]->refreshWaiting) {
                // check to make sure all banks are idle
                bool allIdle = true;

                for (size_t j = 0; j < NUM_BANKS; ++j) {
                    auto &bsij = bankStates[i][j];

                    if (bsij.currentBankState != BankState::Idle) {
                        allIdle = false;
                        break;
                    }
                }

                // if they ARE all idle, put in power down mode and set appropriate fields
                if (allIdle) {
                    powerDown[i] = true;
                    (*ranks)[i]->powerDown();

                    for (size_t j = 0; j < NUM_BANKS; ++j) {
                        auto &bsij = bankStates[i][j];
                        bsij.currentBankState = BankState::PowerDown;
                        bsij.nextPowerUp = currentClockCycle + tCKE;
                    }
                }
            } else if (currentClockCycle >= bankStates[i][0].nextPowerUp && powerDown[i]) {
                // if there IS something in the queue or there IS a refresh waiting (and we can power up), do it
                // use 0 since they are all the same
                powerDown[i] = false;
                (*ranks)[i]->powerUp();

                for (size_t j = 0; j < NUM_BANKS; ++j) {
                    auto &bsij = bankStates[i][j];
                    bsij.currentBankState = BankState::Idle;
                    bsij.nextActivate = currentClockCycle + tXP;
                }
            }
        }

        // check for open bank
        bool bankOpen = false;

        for (size_t j = 0; j < NUM_BANKS; ++j) {
            auto &bsij = bankStates[i][j];

            if (bsij.currentBankState == BankState::Refreshing || bsij.currentBankState == BankState::RowActive) {
                bankOpen = true;
                break;
            }
        }

        // background power is dependent on whether or not a bank is open or not
        if (bankOpen) {
            if (DEBUG_POWER) {
                PRINT(" ++ Adding IDD3N to total energy [from rank " << i << "]");
            }
            backgroundEnergy[i] += IDD3N * NUM_DEVICES;
        } else {
            // if we're in power-down mode, use the correct current
            if (powerDown[i]) {
                if (DEBUG_POWER) {
                    PRINT(" ++ Adding IDD2P to total energy [from rank " << i << "]");
                }
                backgroundEnergy[i] += IDD2P * NUM_DEVICES;
            } else {
                if (DEBUG_POWER) {
                    PRINT(" ++ Adding IDD2N to total energy [from rank " << i << "]");
                }
                backgroundEnergy[i] += IDD2N * NUM_DEVICES;
            }
        }
    }

    /// check for outstanding data to return to the CPU
    if (returnTransaction.size() > 0) {
        if (DEBUG_BUS) {
            PRINTN(" -- MC Issuing to CPU bus : " << *returnTransaction[0]);
        }

        ++totalTransactions;

        bool foundMatch = false;

        // find the pending read transaction to calculate latency
        for (size_t i = 0; i < pendingReadTransactions.size(); i++) {
            if (pendingReadTransactions[i]->address == returnTransaction[0]->address) {
                unsigned chan;
                unsigned rank;
                unsigned bank;
                unsigned row;
                unsigned col;
                addressMapping(returnTransaction[0]->address, chan, rank, bank, row, col);
                insertHistogram(currentClockCycle - pendingReadTransactions[i]->timeAdded, rank, bank);
                // return latency
                returnReadData(pendingReadTransactions[i]);

                delete pendingReadTransactions[i];
                pendingReadTransactions.erase(pendingReadTransactions.begin() + i);
                foundMatch = true;
                break;
            }
        }

        if (!foundMatch) {
            ERROR("Can't find a matching transaction for 0x" << hex << returnTransaction[0]->address << dec);
            throw std::logic_error("Can't find a matching transaction");
        }

        delete returnTransaction[0];
        returnTransaction.erase(returnTransaction.begin());
    }

    // decrement refresh counters
    for (size_t i = 0; i < NUM_RANKS; ++i) {
        --refreshCountdown[i];
    }

    if (DEBUG_TRANS_Q) {
        // print debug
        PRINT("== Printing transaction queue");

        for (size_t i = 0; i < transactionQueue.size(); i++) {
            PRINTN("  " << i << "] " << *transactionQueue[i]);
        }
    }

    if (DEBUG_BANKSTATE) {
        /// @todo move this to BankState.cpp
        PRINT("== Printing bank states (According to MC)");
        for (size_t i = 0; i < NUM_RANKS; i++) {
            for (size_t j = 0; j < NUM_BANKS; j++) {
                auto &bsij = bankStates[i][j];

                if (bsij.currentBankState == BankState::RowActive) {
                    PRINTN("[" << bsij.openRowAddress << "] ");
                } else if (bsij.currentBankState == BankState::Idle) {
                    PRINTN("[idle] ");
                } else if (bsij.currentBankState == BankState::Precharging) {
                    PRINTN("[pre] ");
                } else if (bsij.currentBankState == BankState::Refreshing) {
                    PRINTN("[ref] ");
                } else if (bsij.currentBankState == BankState::PowerDown) {
                    PRINTN("[lowp] ");
                }
            }

            PRINT(""); // effectively just cout<<endl;
        }
    }

    if (DEBUG_CMD_Q) {
        commandQueue.print();
    }

    commandQueue.step();

}

bool
MemoryController::WillAcceptTransaction(void)const
{
    return transactionQueue.size() < TRANS_QUEUE_DEPTH;
}

/// allows outside source to make request of memory system
bool
MemoryController::addTransaction(Transaction *trans)
{
    if (!WillAcceptTransaction()) {
        return false;
    }

    trans->timeAdded = currentClockCycle;
    transactionQueue.push_back(trans);
    return true;
}

void
MemoryController::resetStats(void)
{
    for (size_t i = 0; i < NUM_RANKS; ++i) {
        for (size_t j = 0; j < NUM_BANKS; ++j) {
            /// @todo XXX: this means the bank list won't be printed for partial epochs
            auto const sij = SEQUENTIAL(i, j);
            grandTotalBankAccesses[sij] += totalReadsPerBank[sij] + totalWritesPerBank[sij];
            totalReadsPerBank[sij] = 0;
            totalWritesPerBank[sij] = 0;
            totalEpochLatency[sij] = 0;
        }

        burstEnergy[i] = 0;
        actpreEnergy[i] = 0;
        refreshEnergy[i] = 0;
        backgroundEnergy[i] = 0;
        totalReadsPerRank[i] = 0;
        totalWritesPerRank[i] = 0;
    }
}

/// prints statistics at the end of an epoch or  simulation
void
MemoryController::printStats(bool finalStats)
{
    auto const myChannel = parentMemorySystem->systemID;

    // if we are not at the end of the epoch, make sure to adjust for the actual number of cycles elapsed

    uint64_t const cyclesElapsed = (currentClockCycle % EPOCH_LENGTH == 0) ? EPOCH_LENGTH : currentClockCycle % EPOCH_LENGTH;
    unsigned const bytesPerTransaction = (JEDEC_DATA_BUS_BITS * BL) / 8;
    uint64_t const totalBytesTransferred = totalTransactions * bytesPerTransaction;
    double const secondsThisEpoch = double(cyclesElapsed) * tCK * 1E-9;

    // only per rank
    vector<double> backgroundPower(NUM_RANKS, 0.0);
    vector<double> burstPower(NUM_RANKS, 0.0);
    vector<double> refreshPower(NUM_RANKS, 0.0);
    vector<double> actprePower(NUM_RANKS, 0.0);
    vector<double> averagePower(NUM_RANKS, 0.0);

    // per bank variables
    vector<double> averageLatency(NUM_RANKS * NUM_BANKS, 0.0);
    vector<double> bandwidth(NUM_RANKS * NUM_BANKS, 0.0);

    double totalBandwidth = 0.0;

    for (size_t i = 0; i < NUM_RANKS; ++i) {
        for (size_t j = 0; j < NUM_BANKS; ++j) {
            auto const sij = SEQUENTIAL(i, j);

            bandwidth[sij] = (double(totalReadsPerBank[sij]) + totalWritesPerBank[sij]) * double(bytesPerTransaction) / (1024.0 * 1024.0 * 1024.0) / secondsThisEpoch;
            averageLatency[sij] = float(totalEpochLatency[sij]) / float(totalReadsPerBank[sij]) * tCK;
            totalBandwidth += bandwidth[sij];
            totalReadsPerRank[i] += totalReadsPerBank[sij];
            totalWritesPerRank[i] += totalWritesPerBank[sij];
        }
    }
#ifdef LOG_OUTPUT
    dramsim_log.precision(3);
    dramsim_log.setf(ios::fixed, ios::floatfield);
#else
    cout.precision(3);
    cout.setf(ios::fixed, ios::floatfield);
#endif

    PRINT(" =======================================================");
    PRINT(" ============== Printing Statistics [id:" << parentMemorySystem->systemID << "]==============");
    PRINTN("   Total Return Transactions : " << totalTransactions);
    PRINT(" (" << totalBytesTransferred << " bytes) aggregate average bandwidth " << totalBandwidth << "GB/s");

    double totalAggregateBandwidth = 0.0;

    for (size_t r = 0; r < NUM_RANKS; ++r) {
        PRINT("      -Rank   " << r << " : ");
        PRINTN("        -Reads  : " << totalReadsPerRank[r]);
        PRINT(" (" << totalReadsPerRank[r] * bytesPerTransaction << " bytes)");
        PRINTN("        -Writes : " << totalWritesPerRank[r]);
        PRINT(" (" << totalWritesPerRank[r] * bytesPerTransaction << " bytes)");

        for (size_t j = 0; j < NUM_BANKS; ++j) {
            PRINT("        -Bandwidth / Latency  (Bank " << j << "): " << bandwidth[SEQUENTIAL(r, j)] << " GB/s\t\t" << averageLatency[SEQUENTIAL(r, j)] << " ns");
        }

        // factor of 1000 at the end is to account for the fact that totalEnergy is accumulated in mJ since IDD values are given in mA
        backgroundPower[r] = double(backgroundEnergy[r]) / double(cyclesElapsed) * Vdd / 1000.0;
        burstPower[r] = double(burstEnergy[r]) / double(cyclesElapsed) * Vdd / 1000.0;
        refreshPower[r] = double(refreshEnergy[r]) / double(cyclesElapsed) * Vdd / 1000.0;
        actprePower[r] = double(actpreEnergy[r]) / double(cyclesElapsed) * Vdd / 1000.0;
        averagePower[r] = ((backgroundEnergy[r] + burstEnergy[r] + refreshEnergy[r] + actpreEnergy[r]) / double(cyclesElapsed)) * Vdd / 1000.0;

        if (*parentMemorySystem->ReportPower) {
            (*parentMemorySystem->ReportPower)(backgroundPower[r], burstPower[r], refreshPower[r], actprePower[r]);
        }

        PRINT(" == Power Data for Rank        " << r);
        PRINT("   Average Power (watts)     : " << averagePower[r]);
        PRINT("     -Background (watts)     : " << backgroundPower[r]);
        PRINT("     -Act/Pre    (watts)     : " << actprePower[r]);
        PRINT("     -Burst      (watts)     : " << burstPower[r]);
        PRINT("     -Refresh    (watts)     : " << refreshPower[r]);

        if (VIS_FILE_OUTPUT) {
            //	cout << "c="<<myChannel<< " r="<<r<<"writing to csv out on cycle "<< currentClockCycle<<endl;
                    // write the vis file output
            csvOut << CSVWriter::IndexedName("Background_Power", myChannel, r) << backgroundPower[r];
            csvOut << CSVWriter::IndexedName("ACT_PRE_Power", myChannel, r) << actprePower[r];
            csvOut << CSVWriter::IndexedName("Burst_Power", myChannel, r) << burstPower[r];
            csvOut << CSVWriter::IndexedName("Refresh_Power", myChannel, r) << refreshPower[r];
            double totalRankBandwidth = 0.0;

            for (size_t b = 0; b < NUM_BANKS; ++b) {
                csvOut << CSVWriter::IndexedName("Bandwidth", myChannel, r, b) << bandwidth[SEQUENTIAL(r, b)];
                totalRankBandwidth += bandwidth[SEQUENTIAL(r, b)];
                totalAggregateBandwidth += bandwidth[SEQUENTIAL(r, b)];
                csvOut << CSVWriter::IndexedName("Average_Latency", myChannel, r, b) << averageLatency[SEQUENTIAL(r, b)];
            }
            csvOut << CSVWriter::IndexedName("Rank_Aggregate_Bandwidth", myChannel, r) << totalRankBandwidth;
            csvOut << CSVWriter::IndexedName("Rank_Average_Bandwidth", myChannel, r) << totalRankBandwidth / NUM_RANKS;
        }
    }

    if (VIS_FILE_OUTPUT) {
        csvOut << CSVWriter::IndexedName("Aggregate_Bandwidth", myChannel) << totalAggregateBandwidth;
        csvOut << CSVWriter::IndexedName("Average_Bandwidth", myChannel) << totalAggregateBandwidth / (NUM_RANKS*NUM_BANKS);
    }

    // only print the latency histogram at the end of the simulation since it clogs the output too much to print every epoch
    if (finalStats) {
        PRINT(" ---  Latency list (" << latencies.size() << ")");
        PRINT("       [lat] : #");
        if (VIS_FILE_OUTPUT) {
            csvOut.getOutputStream() << "!!HISTOGRAM_DATA" << endl;
        }

        for (auto const &it : latencies) {
            PRINT("       [" << it.first << "-" << it.first + (HISTOGRAM_BIN_SIZE - 1) << "] : " << it.second);
            if (VIS_FILE_OUTPUT) {
                csvOut.getOutputStream() << it.first << "=" << it.second << endl;
            }
        }

        if (currentClockCycle % EPOCH_LENGTH == 0) {
            PRINT(" --- Grand Total Bank usage list");

            for (size_t i = 0; i < NUM_RANKS; ++i) {
                PRINT("Rank " << i << ":");

                for (size_t j = 0; j < NUM_BANKS; ++j) {
                    auto const sij = SEQUENTIAL(i, j);
                    PRINT("  b" << j << ": " << grandTotalBankAccesses[sij]);
                }
            }
        }

    }

    PRINT(endl << " == Pending Transactions : " << pendingReadTransactions.size() << " (" << currentClockCycle << ") ==");
#if 0
    for(size_t i=0;i<pendingReadTransactions.size();i++)
            {
                    PRINT( i << "] I've been waiting for "<<currentClockCycle-pendingReadTransactions[i].timeAdded<<endl;
            }
#endif
#ifdef LOG_OUTPUT
    dramsim_log.flush();
#endif

    resetStats();
}

MemoryController::~MemoryController(void)
{
    for (auto el: pendingReadTransactions) {
        delete el;
    }

    for (auto el: returnTransaction) {
        delete el;
    }

}

/// inserts a latency into the latency histogram
void
MemoryController::insertHistogram(unsigned const latencyValue,
                                  unsigned const rank,
                                  unsigned const bank)
{
    totalEpochLatency[SEQUENTIAL(rank, bank)] += latencyValue;

    // poor man's way to bin things.
    ++latencies[(latencyValue / HISTOGRAM_BIN_SIZE) * HISTOGRAM_BIN_SIZE];
}

}  // namespace DRAMSim
