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

#include "Callback.hpp"
#include "SimulatorObject.hpp"
#include "IniReader.hpp"
#include "ClockDomain.hpp"

namespace DRAMSim {

class Transaction;
class MemorySystem;
class CSVWriter;

class MultiChannelMemorySystem
    : public SimulatorObject 
{
public:
    MultiChannelMemorySystem(const std::string &dev,
                             const std::string &sys,
                             const std::string &pwd,
                             const std::string &trc,
                             unsigned megsOfMemory,
                             std::string *visFilename = nullptr,
                             const IniReader::OverrideMap *paramOverrides = nullptr);
    virtual
        ~MultiChannelMemorySystem(void);

    bool
        addTransaction(Transaction *trans);
    bool
        addTransaction(const Transaction &trans);
    bool
        addTransaction(bool isWrite,
                       uint64_t addr);
    bool
        willAcceptTransaction(void);
    bool
        willAcceptTransaction(uint64_t addr);
    void
        update();
    void
        printStats(bool finalStats = false);

    std::ostream &
        getLogFile(void)
    {
        return dramsim_log;
    }

    void
        RegisterCallbacks(TransactionCompleteCB *readDone,
                          TransactionCompleteCB *writeDone,
                          void(*reportPower)(double bgpower, double burstpower, double refreshpower, double actprepower));
    int
        getIniBool(const std::string &field,
                   bool *val);
    int
        getIniUint(std::string const &field,
                   unsigned *val);
    int
        getIniUint64(const std::string &field,
                     uint64_t *val);
    int
        getIniFloat(const std::string &field, float *val);

    void
        InitOutputFiles(std::string tracefilename);
    void
        setCPUClockSpeed(uint64_t cpuClkFreqHz);

    //output file
    std::ofstream visDataOut;
    std::ofstream dramsim_log;

private:
    unsigned
        findChannelNumber(uint64_t addr);
    void
        actual_update(void);
    static void
        mkdirIfNotExist(std::string const& path);
    static bool
        fileExists(std::string path);

    std::vector<MemorySystem*> channels;
    unsigned megsOfMemory;
    std::string deviceIniFilename;
    std::string systemIniFilename;
    std::string traceFilename;
    std::string pwd;
    std::string *visFilename;
    ClockDomain::ClockDomainCrosser clockDomainCrosser;
    CSVWriter *csvOut;
};

}  // namespace DRAMSim
