/**
 * Main file for the Agent QA test module
 */

#define BOOST_TEST_MODULE AgentTests

#include <boost/test/unit_test.hpp>
#include <cstdlib>

// Set AGENT_PYTHON_DIR before any PYTHON_TOOL_HANDLER is constructed.
// QA_AGENT_PYTHON_DIR is baked in at compile time by CMake.
struct PythonDirFixture
{
    PythonDirFixture()  { setenv( "AGENT_PYTHON_DIR", QA_AGENT_PYTHON_DIR, 1 ); }
};

BOOST_GLOBAL_FIXTURE( PythonDirFixture );
