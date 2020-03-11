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

#include "Bank.hpp"

#include "SystemConfiguration.hpp"

namespace DRAMSim {
using namespace std;

Bank::Bank(ostream &dramsim_log_)
    : currentState(dramsim_log_)
    , rowEntries(NUM_COLS)
    , dramsim_log(dramsim_log_)
{}

/** The bank class is just a glorified sparse storage data structure
 * that keeps track of written data in case the simulator wants a
 * function DRAM model
 *
 * A vector of size NUM_COLS keeps a linked list of rows and their
 * associated values.
 *
 * write() adds an entry to the proper linked list or replaces the
 * 	value in a row that was already written
 *
 * read() searches for a node with the right row value, if not found
 * 	returns the tracer value 0xDEADBEEF
 *
 * @todo if anyone wants to actually store data, see the 'data_storage' branch and perhaps try to merge that into master
 * @todo use std
 * @bug this is a DataStruct list method rather than Bank method
 */
Bank::DataStruct *
Bank::searchForRow(unsigned const row,
                   DataStruct *head)
{
    /// @todo use std::find
    for (; head; head = head->next) {
        if (head->row == row) {
            break;
        }
    }

    return head;
}

void
Bank::read(BusPacket *const busPacket)
{
    auto const rowHeadNode = this->rowEntries[busPacket->column];
    auto const foundNode = Bank::searchForRow(busPacket->row, rowHeadNode);

    if (!foundNode) {
        // the row hasn't been written before, so it isn't in the list
        // if(SHOW_SIM_OUTPUT) DEBUG("== Warning - Read from previously unwritten row " << busPacket->row);
        void *garbage = calloc(BL * (JEDEC_DATA_BUS_BITS / 8), 1);
        /// @bug endian
        static_cast<long *>(garbage)[0] = 0xdeadbeef;  // tracer value
        busPacket->data = garbage;
    } else {
        // found it
        busPacket->data = foundNode->data;
    }

    // the return packet should be a data packet, not a read packet
    busPacket->busPacketType = DATA;
}

void
Bank::write(const BusPacket *const busPacket)
{
    /// @todo move all the error checking to BusPacket so once we have a bus packet,
    ///			we know the fields are all legal

    if (busPacket->column >= NUM_COLS) {
        ERROR("== Error - Bus Packet column " << busPacket->column << " out of bounds");
        throw std::logic_error("== Error - Bus Packet column ");
    }

    // head of the list we need to search
    auto const rowHeadNode = rowEntries[busPacket->column];
    auto const foundNode = Bank::searchForRow(busPacket->row, rowHeadNode);

    if (!foundNode) {
        // not found
        auto const newRowNode = new DataStruct;

        /// insert at the head for speed
        /// @todo Optimize this data structure for speedier lookups?
        newRowNode->row = busPacket->row;
        newRowNode->data = busPacket->data;
        newRowNode->next = rowHeadNode;
        rowEntries[busPacket->column] = newRowNode;
    } else {
        // found it, just plaster in the new data
        foundNode->data = busPacket->data;

        if (DEBUG_BANKS) {
            PRINTN(" -- Bank " << busPacket->bank << " writing to physical address 0x" << hex << busPacket->physicalAddress << dec << ":");
            busPacket->printData();
            PRINT("");
        }
    }
}
}  // namespace DRAMSim
