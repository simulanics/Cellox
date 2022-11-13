#include "test_cellox.hh"

#include "gtest/gtest.h"

#include "init.h"
#include "virtual_machine.h"

/// @brief Test a cellox program
/// @param programPath The path of the program that is tested
/// @param expectedOutput The expected output of the program
/// @param producesError Determines wheather the rpogram leads to a runtime/compiler error
static void test_program(std::string const & programPath, std::string const & expectedOutput, bool producesError);

void test_cellox_program(std::string const & programPath, std::string const & expectedOutput)
{
    test_program(programPath, expectedOutput, false);
}

void test_failing_cellox_program(std::string const & programPath, std::string const & expectedOutput)
{
    test_program(programPath, expectedOutput, true);
}

static void test_program(std::string const & programPath, std::string const & expectedOutput, bool producesError)
{
    // Create absolute filepath
    std::string filePath = TEST_PROGRAM_BASE_PATH;
    filePath.append(programPath);
    
    // Redirect output
    char actual_output [1024];
    for (size_t i = 0; i < 1024; i++)
        actual_output[i] = '\0';
    if(producesError)
    {
        #ifdef _WIN32
        freopen("NUL", "a", stderr);
        #endif
        #ifdef linux
        freopen("/dev/nul", "a", stderr);
        #endif
        setbuf(stderr, actual_output);
    }
    else
    {
        #ifdef _WIN32
        freopen("NUL", "a", stdout);
        #endif
        #ifdef linux
        freopen("/dev/nul", "a", stdout);
        #endif
        setbuf(stdout, actual_output);
    }
    
    virtual_machine_init();
    // Execute Test 🚀
    init_run_from_file(filePath.c_str(), false);

    if(producesError)
    {
        // Reset stderr redirection
        #ifdef _WIN32
        freopen("CON", "w", stderr);
        #endif
        #ifdef linux
        freopen("CON", "w", stderr);
        #endif
    }
    else
    {   
        // Free vm
        virtual_machine_free();
        // Reset stdout redirection
        #ifdef _WIN32
        freopen("CON", "w", stdout);
        #endif
        #ifdef linux
        freopen("CON", "w", stdout);
        #endif
    }

    ASSERT_STREQ(expectedOutput.c_str(), actual_output);
}