/** @
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
#ifndef CSV_WRITER_HPP_
#define CSV_WRITER_HPP_

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>

/** @file
 * CSVWriter: Writes CSV data with headers to an underlying ofstream 
 * 	This wrapper is meant to look like an ofstream, but it captures 
 * 	the names of each field and prints it out to a header before printing
 * 	the CSV data below. 
 *
 * 	Note: the first finalize() will not print the values out, only the headers.
 * 	One way to fix this problem would be to use a sstringstream (or something) 
 * 	to buffer out the values and flush them all in one go instead of passing them 
 * 	directly to the underlying stream as is the case now. 
 *
 * 	Example usage: 
 *
 * 	CSVWriter sw(cout);               // send output to cout
 * 	sw <<"Bandwidth" << 0.5; // value ignored
 * 	sw <<"Latency" << 5;     // value ignored
 * 	sw.finalize();                      // flush the header 
 * 	sw <<"Bandwidth" << 1.5; // field name ignored
 * 	sw <<"Latency" << 15;     // field name ignored
 * 	sw.finalize(); 							// values printed to csv line
 * 	sw <<"Bandwidth" << 2.5; // field name ignored
 * 	sw <<"Latency" << 25;     // field name ignored
 * 	sw.finalize(); 							// values printed to csv line
 *
 * 	The output of this example will be: 
 *
 * 	Bandwidth,Latency
 * 	1.5,15
 * 	2.5,25
 *
 */

namespace DRAMSim {

class CSVWriter
{
public:
    struct IndexedName
    {
        static const size_t MAX_TMP_STR = 64;
        static const unsigned SINGLE_INDEX_LEN = 4;
        std::string str;

        static bool
            isNameTooLong(const char *baseName,
                          unsigned numIndices)
        {
            return (strlen(baseName) + (numIndices * SINGLE_INDEX_LEN)) > MAX_TMP_STR;
        }

        static void
            checkNameLength(const char *baseName, unsigned numIndices)
        {
            if (isNameTooLong(baseName, numIndices)) {
                ERROR("Your string " << baseName << " is too long for the max stats size (" << MAX_TMP_STR << ", increase MAX_TMP_STR");
                throw std::logic_error("Your string is too long for the max stats size");
            }
        }

        IndexedName(const char *baseName,
                    unsigned channel)
        {
            checkNameLength(baseName, 1);
            char tmp_str[MAX_TMP_STR];
            snprintf(tmp_str, MAX_TMP_STR, "%s[%u]", baseName, channel);
            str = std::string(tmp_str);
        }

        IndexedName(const char *baseName,
                    unsigned channel,
                    unsigned rank)
        {
            checkNameLength(baseName, 2);
            char tmp_str[MAX_TMP_STR];
            snprintf(tmp_str, MAX_TMP_STR, "%s[%u][%u]", baseName, channel, rank);
            str = std::string(tmp_str);
        }

        IndexedName(const char *baseName,
                    unsigned channel,
                    unsigned rank,
                    unsigned bank)
        {
            checkNameLength(baseName, 3);
            char tmp_str[MAX_TMP_STR];
            snprintf(tmp_str, MAX_TMP_STR, "%s[%u][%u][%u]", baseName, channel, rank, bank);
            str = std::string(tmp_str);
        }

    };

    // where the output will eventually go
    std::ostream &output;
    std::vector<std::string> fieldNames;
    bool finalized;
    unsigned idx;

public:
    void
        finalize(void)
    {
        /// @todo tag unlikely

        if (!finalized) {
            for (unsigned i = 0; i < fieldNames.size(); i++) {
                output << fieldNames[i] << ",";
            }

            output << std::endl << std::flush;
            finalized = true;
        } else {
            if (idx < fieldNames.size()) {
                printf(" Number of fields doesn't match values (fields=%u, values=%u), check each value has a field name before it\n", idx, (unsigned)fieldNames.size());
            }

            idx = 0;
            output << std::endl;
        }
    }

    CSVWriter(std::ostream & _output)
        : output(_output)
        , finalized(false)
        , idx(0)
    {}

    // Insertion operators for field names
    CSVWriter &
        operator<<(const char *name)
    {
        if (!finalized) {
            fieldNames.push_back(std::string(name));
        }

        return *this;
    }

    CSVWriter &
        operator<<(const std::string &name)
    {
        if (!finalized) {
            fieldNames.push_back(std::string(name));
        }

        return *this;
    }

    CSVWriter &
        operator<<(const IndexedName &indexedName)
    {
        if (!finalized) {
            fieldNames.push_back(indexedName.str);
        }

        return *this;
    }

    bool
        isFinalized(void)const
    {
        return finalized;
    }

    std::ostream &
        getOutputStream(void)const
    {
        return output;
    }

    /// Insertion operators for value types
    /// All of the other types just need to pass through to the underlying
    /// ofstream, so just write this small wrapper function to make the
    /// whole thing less verbose
    template <typename T>
    CSVWriter &
        operator<<(T const& value)
    {
        if (finalized) {
            output << value << ",";
            ++idx;
        }

        return *this;
    }

private:
    /// disable copy constructor and assignment operator
    CSVWriter(const CSVWriter &) = delete;
    CSVWriter &operator=(const CSVWriter &) = delete;

};
} // namespace DRAMSim

#endif  // CSV_WRITER_HPP_
