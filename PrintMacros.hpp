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

#ifndef PRINT_MACROS_HPP
#define PRINT_MACROS_HPP

#include <iostream>

namespace DRAMSim {
extern int SHOW_SIM_OUTPUT;
}  // namespace DRAMSim

#define ERROR(STR) do {std::cerr << "[ERROR (" << __FILE__ << ":" << __LINE__ << ")]: " << STR << std::endl; } while (0)

#ifdef DEBUG_BUILD
#define DEBUG(str)  do {std::cerr<< str <<std::endl; } while (0)
#define DEBUGN(str) do {std::cerr<< str;} while (0)
#else  // DEBUG_BUILD
#define DEBUG(str) do {} while (0)
#define DEBUGN(str) do {} while (0)
#endif  // DEBUG_BUILD

#ifdef NO_OUTPUT
#undef DEBUG
#undef DEBUGN
#define DEBUG(str) do {} while (0)
#define DEBUGN(str) do {} while (0)
#define PRINT(str) do {} while (0)
#define PRINTN(str) do {} while (0)
#else  // NO_OUTPUT
#ifdef LOG_OUTPUT
#define PRINT(str)  do { dramsim_log <<str<<std::endl; } while (0)
#define PRINTN(str) do { dramsim_log <<str; } while (0)
#else  // LOG_OUTPUT
#define PRINT(str)  do {if(SHOW_SIM_OUTPUT) { std::cout <<str<<std::endl; }} while (0)
#define PRINTN(str) do {if(SHOW_SIM_OUTPUT) { std::cout <<str; }} while (0)
#endif  // LOG_OUTPUT
#endif  // NO_OUTPUT

#endif  // PRINT_MACROS_HPP
