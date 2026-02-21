# This program source code file is part of KiCad, a free EDA CAD application.
#
# Copyright The KiCad Developers, see AUTHORS.txt for contributors.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, you may find one here:
# http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
# or you may search the http://www.gnu.org website for the version 2 license,
# or you may write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA

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
