#include "SystemConfiguration.hpp"

#include <fstream>

namespace DRAMSim {
std::ofstream cmd_verify_out; // used in Rank.cpp and MemoryController.cpp if VERIFICATION_OUTPUT is set

bool VERIFICATION_OUTPUT;
bool DEBUG_TRANS_Q = false;
bool DEBUG_CMD_Q;
bool DEBUG_ADDR_MAP;
bool DEBUG_BANKSTATE;
bool DEBUG_BUS;
bool DEBUG_BANKS;
bool DEBUG_POWER;
bool USE_LOW_POWER;
bool VIS_FILE_OUTPUT;
uint64_t TOTAL_STORAGE;
unsigned NUM_BANKS;
unsigned NUM_BANKS_LOG;
unsigned NUM_RANKS;
unsigned NUM_RANKS_LOG;
unsigned NUM_CHANS;
unsigned NUM_CHANS_LOG;
unsigned NUM_ROWS;
unsigned NUM_ROWS_LOG;
unsigned NUM_COLS;
unsigned NUM_COLS_LOG;
unsigned DEVICE_WIDTH;
unsigned BYTE_OFFSET_WIDTH;
unsigned TRANSACTION_SIZE;
unsigned THROW_AWAY_BITS;
unsigned COL_LOW_BIT_WIDTH;
unsigned REFRESH_PERIOD;
float tCK;
unsigned CL;
unsigned AL;
unsigned BL;
unsigned tRAS;
unsigned tRCD;
unsigned tRRD;
unsigned tRC;
unsigned tRP;
unsigned tCCD;
unsigned tRTP;
unsigned tWTR;
unsigned tWR;
unsigned tRTRS;
unsigned tRFC;
unsigned tFAW;
unsigned tCKE;
unsigned tXP;
unsigned tCMD;
unsigned NUM_DEVICES;

/// in bytes
unsigned JEDEC_DATA_BUS_BITS;

/// Memory Controller related parameters
///@{
unsigned TRANS_QUEUE_DEPTH;
unsigned CMD_QUEUE_DEPTH;
///@}

/// cycles within an epoch
unsigned EPOCH_LENGTH;

/// row accesses allowed before closing (open page)
unsigned TOTAL_ROW_ACCESSES;

/// strings and their associated enums
std::string ROW_BUFFER_POLICY;
std::string SCHEDULING_POLICY;
std::string ADDRESS_MAPPING_SCHEME;
std::string QUEUING_STRUCTURE;

RowBufferPolicy rowBufferPolicy;
SchedulingPolicy schedulingPolicy;
AddressMappingScheme addressMappingScheme;
QueuingStructure queuingStructure;

unsigned IDD0;
unsigned IDD1;
unsigned IDD2P;
unsigned IDD2Q;
unsigned IDD2N;
unsigned IDD3Pf;
unsigned IDD3Ps;
unsigned IDD3N;
unsigned IDD4W;
unsigned IDD4R;
unsigned IDD5;
unsigned IDD6;
unsigned IDD6L;
unsigned IDD7;

float Vdd;

}  // namespace DRAMSim
