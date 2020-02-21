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
#include "MultiChannelMemorySystem.hpp"
#include "AddressMapping.hpp"
#include "MemorySystem.hpp"

// for directory operations 
#include <sys/stat.h>
#include <sys/types.h>

#include <cerrno> 
#include <sstream>

namespace DRAMSim {

using namespace std;

MultiChannelMemorySystem::MultiChannelMemorySystem(const string &deviceIniFilename_,
                                                   const string &systemIniFilename_,
                                                   const string &pwd_,
                                                   const string &traceFilename_,
                                                   unsigned megsOfMemory_,
                                                   string *visFilename_,
                                                   const IniReader::OverrideMap *paramOverrides)
    : megsOfMemory(megsOfMemory_)
    , deviceIniFilename(deviceIniFilename_)
    , systemIniFilename(systemIniFilename_)
    , traceFilename(traceFilename_)
    , pwd(pwd_)
    , visFilename(visFilename_)
    , clockDomainCrosser(new ClockDomain::Callback<MultiChannelMemorySystem, void>(this, &MultiChannelMemorySystem::actual_update))
    , csvOut(new CSVWriter(visDataOut))
{
    currentClockCycle = 0;

    if (this->visFilename) {
        printf("CC VISFILENAME=%s\n", this->visFilename->c_str());
    }

    if (!isPowerOfTwo(megsOfMemory)) {
        throw std::logic_error("Please specify a power of 2 memory size");
    }

    if (pwd.length() > 0) {
        //ignore the pwd argument if the argument is an absolute path
        if (this->deviceIniFilename[0] != '/') {
            this->deviceIniFilename = pwd + "/" + deviceIniFilename;
        }

        if (this->systemIniFilename[0] != '/') {
            this->systemIniFilename = pwd + "/" + this->systemIniFilename;
        }
    }

    DEBUG("== Loading device model file '" << this->deviceIniFilename << "' == ");
    IniReader::ReadIniFile(this->deviceIniFilename, false);
    DEBUG("== Loading system model file '" << this->systemIniFilename << "' == ");
    IniReader::ReadIniFile(this->systemIniFilename, true);

    // If we have any overrides, set them now before creating all of the memory objects
    if (paramOverrides) {
        IniReader::OverrideKeys(paramOverrides);
    }

    IniReader::InitEnumsFromStrings();

    if (!IniReader::CheckIfAllSet()) {
        throw std::logic_error("!IniReader::CheckIfAllSet");
    }

    if (NUM_CHANS == 0) {
        throw std::logic_error("Zero channels");
    }

    for (size_t i = 0; i < NUM_CHANS; ++i) {
        channels.push_back(new MemorySystem(i, megsOfMemory / NUM_CHANS, *csvOut, dramsim_log));
    }
}

/** Initialize the ClockDomainCrosser to use the CPU speed
    If cpuClkFreqHz == 0, then assume a 1:1 ratio (like for TraceBasedSim)
*/
void
MultiChannelMemorySystem::setCPUClockSpeed(uint64_t const cpuClkFreqHz)
{
    uint64_t const dramsimClkFreqHz = static_cast<uint64_t>(1.0 / (tCK * 1e-9));
    clockDomainCrosser.clock1 = dramsimClkFreqHz;
    clockDomainCrosser.clock2 = cpuClkFreqHz == 0 ? dramsimClkFreqHz : cpuClkFreqHz;
}

static bool
fileExists(string const &path)
{
    struct stat stat_buf;

    if (stat(path.c_str(), &stat_buf) != 0) {
        if (errno == ENOENT) {
            return false;
        }

        ERROR("Warning: some other kind of error happened with stat(), should probably check that");
    }

    return true;
}

static string
FilenameWithNumberSuffix(const string &filename,
                         const string &extension,
                         unsigned const maxNumber = 100)
{
    string currentFilename = filename + extension;

    if (!fileExists(currentFilename)) {
        return currentFilename;
    }

    // otherwise, add the suffixes and test them out until we find one that works
    stringstream tmpNum;
    tmpNum << "." << 1;

    for (unsigned i = 1; i < maxNumber; ++i) {
        currentFilename = filename + tmpNum.str() + extension;

        if (!fileExists(currentFilename)) {
            return currentFilename;
        }

        currentFilename = filename;
        tmpNum.seekp(0);
        tmpNum << "." << i;
    }

    // if we can't find one, just give up and return whatever is the current filename
    ERROR("Warning: Couldn't find a suitable suffix for " << filename);
    return currentFilename;
}

/**
 * This function creates up to 3 output files:
 * 	- The .log file if LOG_OUTPUT is set
 * 	- the .vis file where csv data for each epoch will go
 * 	- the .tmp file if verification output is enabled
 * The results directory is setup to be in PWD/TRACEFILENAME.[SIM_DESC]/DRAM_PARTNAME/PARAMS.vis
 * The environment variable SIM_DESC is also appended to output files/directories
 *
 * @todo verification info needs to be generated per channel so it has to be moved back to MemorySystem
 */
void
MultiChannelMemorySystem::InitOutputFiles(string traceFilename)
{
    auto deviceIniFilenameLength = deviceIniFilename.length();

    char *const sim_description = getenv("SIM_DESC");
    auto const sim_description_str = sim_description ? string(sim_description) : string();
    string deviceName;

    // create a properly named verification output file if need be and open it
    // as the stream 'cmd_verify_out'
    if (VERIFICATION_OUTPUT) {
        string basefilename = deviceIniFilename.substr(deviceIniFilename.find_last_of("/") + 1);
        string verify_filename = "sim_out_" + basefilename;
        if (sim_description) {
            verify_filename += "." + sim_description_str;
        }
        verify_filename += ".tmp";
        cmd_verify_out.open(verify_filename.c_str());

        if (!cmd_verify_out) {
            throw std::logic_error(std::string("Cannot open ") + verify_filename);
        }
    }

    // This sets up the vis file output along with the creating the result
    // directory structure if it doesn't exist
    if (VIS_FILE_OUTPUT) {
        stringstream out;
        string path;

        if (!visFilename) {
            path = "results/";
            // chop off the .ini if it's there
            if (deviceIniFilename.substr(deviceIniFilenameLength - 4) == ".ini") {
                deviceName = deviceIniFilename.substr(0, deviceIniFilenameLength - 4);
                deviceIniFilenameLength -= 4;
            }

            // chop off everything past the last / (i.e. leave filename only)
            {
                auto const lastSlash = deviceName.find_last_of("/");

                if (lastSlash != string::npos) {
                    deviceName = deviceName.substr(lastSlash + 1, deviceIniFilenameLength - lastSlash - 1);
                }
            }

            string rest;
            {
                // working backwards, chop off the next piece of the directory
                auto const lastSlash = traceFilename.find_last_of("/");

                if (lastSlash != string::npos) {
                    traceFilename = traceFilename.substr(lastSlash + 1, traceFilename.length() - lastSlash - 1);
                }
            }

            if (sim_description) {
                traceFilename += "." + sim_description_str;
            }

            if (pwd.length() > 0) {
                path = pwd + "/" + path;
            }

            // create the directories if they don't exist 
            mkdirIfNotExist(path);
            path += traceFilename + "/";
            mkdirIfNotExist(path);
            path += deviceName + "/";
            mkdirIfNotExist(path);

            // finally, figure out the filename
            string sched = "BtR";
            if (schedulingPolicy == RankThenBankRoundRobin) {
                sched = "RtB";
            }
            string queue = queuingStructure == PerRankPerBank ? "pRankpBank" : "pRank";

            /* I really don't see how "the C++ way" is better than snprintf()  */
            out << (TOTAL_STORAGE >> 10) << "GB." << NUM_CHANS << "Ch." << NUM_RANKS << "R." << ADDRESS_MAPPING_SCHEME << "." << ROW_BUFFER_POLICY << "." << TRANS_QUEUE_DEPTH << "TQ." << CMD_QUEUE_DEPTH << "CQ." << sched << "." << queue;
        } else {
            // visFilename given
            out << *visFilename;
        }

        if (sim_description) {
            out << "." << sim_description;
        }

        // filename so far, without extension, see if it exists already
        path.append(FilenameWithNumberSuffix(out.str(), ".vis"));
        cerr << "writing vis file to " << path << endl;
        visDataOut.open(path.c_str());

        if (!visDataOut) {
            ERROR("Cannot open '" << path << "'");
            throw std::runtime_error("Cannot open");
        }

        //write out the ini config values for the visualizer tool
        IniReader::WriteValuesOut(visDataOut);
    }

#ifdef LOG_OUTPUT
    string dramsimLogFilename("dramsim");

    if (sim_description) {
        dramsimLogFilename += "." + sim_description_str;
    }

    dramsimLogFilename = FilenameWithNumberSuffix(dramsimLogFilename, ".log");

    dramsim_log.open(dramsimLogFilename.c_str(), ios_base::out | ios_base::trunc);

    if (!dramsim_log) {
        ERROR("Cannot open " << dramsimLogFilename);
    }
#endif  // LOG_OUTPUT
}

void
MultiChannelMemorySystem::mkdirIfNotExist(string const &path)
{
    struct stat stat_buf;
    // check if the directory exists
    // nonzero return value on error, check errno
    if (stat(path.c_str(), &stat_buf) != 0) {
        if (errno != ENOENT) {
            throw std::logic_error("Something else when wrong");
        }
        // set permissions dwxr-xr-x on the results directories
        mode_t mode = S_IXOTH | S_IXGRP | S_IXUSR | S_IROTH | S_IRGRP | S_IRUSR | S_IWUSR;

        if (mkdir(path.c_str(), mode) != 0) {
            cerr << "Error Has occurred while trying to make directory: " << path << endl;
            throw std::logic_error("Error Has occurred while trying to make directory");
        }
    } else {
        // directory already exists
        if (!S_ISDIR(stat_buf.st_mode)) {
            throw std::logic_error(path + " is not a directory");
        }
    }
}

MultiChannelMemorySystem::~MultiChannelMemorySystem(void)
{
    for (auto const &channel: this->channels) {
        delete channel;
    }

    this->channels.clear();

    // flush our streams and close them up
#ifdef LOG_OUTPUT
    this->dramsim_log.flush();
    this->dramsim_log.close();
#endif  // LOG_OUTPUT

    if (VIS_FILE_OUTPUT) {
        this->visDataOut.flush();
        this->visDataOut.close();
    }
}

void
MultiChannelMemorySystem::update(void)
{
    this->clockDomainCrosser.update();
}

void
MultiChannelMemorySystem::actual_update(void)
{
    if (currentClockCycle == 0) {
        InitOutputFiles(traceFilename);
        DEBUG("DRAMSim2 Clock Frequency =" << clockDomainCrosser.clock1 << "Hz, CPU Clock Frequency=" << clockDomainCrosser.clock2 << "Hz");
    }

    if (currentClockCycle % EPOCH_LENGTH == 0) {
        (*csvOut) << "ms" << currentClockCycle * tCK * 1E-6;

        for (auto const channel: this->channels) {
            channel->printStats(false);
        }

        csvOut->finalize();
    }

    for (auto const channel : this->channels) {
        channel->update();
    }

    ++currentClockCycle;
}

unsigned
MultiChannelMemorySystem::findChannelNumber(uint64_t addr)
{
    // Single channel case is a trivial shortcut case 
    if (NUM_CHANS == 1) {
        return 0;
    }

    if (!isPowerOfTwo(NUM_CHANS)) {
        throw std::logic_error("We can only support power of two # of channels.\n"
                               "I don't know what Intel was thinking, but trying to address map half a bit is a neat trick that we're not sure how to do");
    }

    // only chan is used from this set 
    unsigned channelNumber;
    unsigned rank;
    unsigned bank;
    unsigned row;
    unsigned col;
    addressMapping(addr, channelNumber, rank, bank, row, col);

    if (channelNumber >= NUM_CHANS) {
        throw std::logic_error(std::string("Got channel index ") + std::to_string(channelNumber) + " but only " + std::to_string(NUM_CHANS) + " exist");
    }

    return channelNumber;
}

bool
MultiChannelMemorySystem::addTransaction(const Transaction &trans)
{
    // copy the transaction and send the pointer to the new transaction 
    return this->addTransaction(new Transaction(trans));
}

bool
MultiChannelMemorySystem::addTransaction(Transaction *const trans)
{
    return this->channels[this->findChannelNumber(trans->address)]->addTransaction(trans);
}

bool
MultiChannelMemorySystem::addTransaction(bool const isWrite,
                                         uint64_t const addr)
{
    return this->channels[this->findChannelNumber(addr)]->addTransaction(isWrite, addr);
}

/**
    This function has two flavors: one with and without the address.
    If the simulator won't give us an address and we have multiple channels,
    we have to assume the worst and return false if any channel won't accept.

    However, if the address is given, we can just map the channel and check just
    that memory controller
*/
bool
MultiChannelMemorySystem::willAcceptTransaction(uint64_t const addr)
{
    unsigned chan;
    unsigned rank;
    unsigned bank;
    unsigned row;
    unsigned col;
    addressMapping(addr, chan, rank, bank, row, col);
    return this->channels[chan]->WillAcceptTransaction();
}

bool
MultiChannelMemorySystem::willAcceptTransaction(void)
{
    for (auto channel: this->channels) {
        if (!channel->WillAcceptTransaction()) {
            return false;
        }
    }

    return true;
}

void
MultiChannelMemorySystem::printStats(bool const finalStats)
{
    (*csvOut) << "ms" << currentClockCycle * tCK * 1E-6;

    for (size_t i = 0; i < NUM_CHANS; i++) {
        PRINT("==== Channel [" << i << "] ====");
        channels[i]->printStats(finalStats);
        PRINT("//// Channel [" << i << "] ////");
    }

    csvOut->finalize();
}

void
MultiChannelMemorySystem::RegisterCallbacks(TransactionCompleteCB *readDone,
                                            TransactionCompleteCB *writeDone,
                                            void(*reportPower)(double bgpower, double burstpower, double refreshpower, double actprepower))
{
    for (auto channel: this->channels) {
        channel->RegisterCallbacks(readDone, writeDone, reportPower);
    }
}

/**
 * The getters below are useful to external simulators interfacing with DRAMSim
 *
 * @return value: 0 on success, -1 on error
 */
int
MultiChannelMemorySystem::getIniBool(std::string const &field,
                                     bool *const val)
{
    if (!IniReader::CheckIfAllSet()) {
        throw std::logic_error("!IniReader::CheckIfAllSet");
    }

    return IniReader::getBool(field, val);
}

int
MultiChannelMemorySystem::getIniUint(const std::string& field,
                                     unsigned *const val)
{
    if (!IniReader::CheckIfAllSet()) {
        throw std::logic_error("!IniReader::CheckIfAllSet()");
    }

    return IniReader::getUint(field, val);
}

int
MultiChannelMemorySystem::getIniUint64(std::string const &field,
                                       uint64_t *const val)
{
    if (!IniReader::CheckIfAllSet()) {
        throw std::logic_error("!IniReader::CheckIfAllSet()");
    }

    return IniReader::getUint64(field, val);
}

int
MultiChannelMemorySystem::getIniFloat(std::string const &field,
                                      float *val)
{
    if (!IniReader::CheckIfAllSet()) {
        throw std::logic_error("!IniReader::CheckIfAllSet()");
    }

    return IniReader::getFloat(field, val);
}

MultiChannelMemorySystem *
getMemorySystemInstance(string const &dev,
                        string const &sys,
                        string const &pwd,
                        string const &trc,
                        unsigned const megsOfMemory,
                        string *const visfilename)
{
    return new MultiChannelMemorySystem(dev, sys, pwd, trc, megsOfMemory, visfilename);
}

}  // namespace DRAMSim
