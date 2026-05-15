find_package( Boost COMPONENTS unit_test_framework REQUIRED )
find_package( wxWidgets 3.0.0 COMPONENTS core base REQUIRED )

set( QA_AGENT_SRCS
    # Test module entry point
    test_module.cpp

    # Library symbol handler tests
    test_sch_lib_symbol_handler.cpp

    # Connect net handler tests
    test_sch_connect_net_handler.cpp

    # CRUD handler tests (sch_delete query-based deletion)
    test_sch_crud_handler.cpp

    # Agent source files needed for tests
    ${CMAKE_SOURCE_DIR}/agent/tools/handlers/python_tool_handler.cpp
)

add_executable( qa_agent
    ${QA_AGENT_SRCS}
)

target_include_directories( qa_agent PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/agent
    ${CMAKE_SOURCE_DIR}/agent/tools
    ${CMAKE_SOURCE_DIR}/eeschema
    ${CMAKE_SOURCE_DIR}/thirdparty/nlohmann_json
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries( qa_agent PRIVATE
    ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY}
    ${wxWidgets_LIBRARIES}
    kicommon
)

target_compile_definitions( qa_agent PRIVATE
    BOOST_TEST_DYN_LINK
    QA_AGENT_PYTHON_DIR="${CMAKE_SOURCE_DIR}/agent/tools/python"
)

# Register with CTest
add_test( NAME qa_agent
    COMMAND qa_agent
)

setup_qa_env( qa_agent )
