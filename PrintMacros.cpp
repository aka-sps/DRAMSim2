#include "PrintMacros.hpp"

/*
 * Enable or disable PRINT() statements.
 *
 * Set by flag in TraceBasedSim.cpp when compiling standalone DRAMSim tool.
 *
 * The DRAMSim libraries do not include the TraceBasedSim object and thus
 * library users can optionally override the weak definition below.
 */
namespace DRAMSim {
#ifndef _SIM_
int SHOW_SIM_OUTPUT = 1;
#else
int SHOW_SIM_OUTPUT = 0;
#endif
}  // namespace DRAMSim
