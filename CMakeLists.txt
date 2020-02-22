cmake_minimum_required(VERSION 3.14)

project(DRAMSim2)

set(DRAMSIM_NO_STORAGE ON CACHE BOOL "")

# option(BUILD_SHARED_LIBS "" ON)

enable_testing()

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/AddressMapping.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Bank.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BankState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BusPacket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ClockDomain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CommandQueue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/IniReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MemoryController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MemorySystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MultiChannelMemorySystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PrintMacros.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Rank.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SimulatorObject.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SystemConfiguration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transaction.cpp
)

set(HEADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/AddressMapping.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Bank.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BankState.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BusPacket.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Callback.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ClockDomain.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CommandQueue.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CSVWriter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DRAMSim.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/IniReader.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MemoryController.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MemorySystem.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MultiChannelMemorySystem.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PrintMacros.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Rank.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SimulatorObject.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SystemConfiguration.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transaction.hpp
)

add_library(dramsim ${SRC_FILES} ${HEADER_FILES})
set_target_properties(dramsim PROPERTIES FOLDER "Libs")
target_include_directories(dramsim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(DRAMSimulator
    ${CMAKE_CURRENT_SOURCE_DIR}/TraceBasedSim.cpp
    ${SRC_FILES}
    ${HEADER_FILES}
)

if(DRAMSIM_NO_STORAGE)
    target_compile_definitions(dramsim PUBLIC NO_STORAGE)
    target_compile_definitions(DRAMSimulator PUBLIC NO_STORAGE)
endif(DRAMSIM_NO_STORAGE)

add_executable(dramsim_test
    ${CMAKE_CURRENT_SOURCE_DIR}/example_app/dramsim_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/example_app/dramsim_test.hpp
)

target_link_libraries(dramsim_test PRIVATE dramsim)

add_test(NAME test_dramsim
         COMMAND $<TARGET_FILE:dramsim_test>)

set_tests_properties(test_dramsim PROPERTIES DEPENDS dramsim_test)
