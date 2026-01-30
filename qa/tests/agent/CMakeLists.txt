# This program source code file is part of KiCad, a free EDA CAD application.
#
# Copyright The KiCad Developers, see AUTHORS.TXT for contributors.
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

find_package( wxWidgets 3.0.0 COMPONENTS core base REQUIRED )

set( QA_AGENT_SRCS
    # Test entry point
    test_module.cpp

    # State machine tests
    test_agent_state.cpp

    # Chat controller tests
    test_chat_controller.cpp

    # Agent source files needed for tests
    ${CMAKE_SOURCE_DIR}/agent/agent_state.cpp
    ${CMAKE_SOURCE_DIR}/agent/agent_events.cpp
    ${CMAKE_SOURCE_DIR}/agent/core/chat_controller.cpp
    ${CMAKE_SOURCE_DIR}/agent/core/agent_tools.cpp
)

# Test executable for agent module
add_executable( qa_agent
    ${QA_AGENT_SRCS}
)

target_include_directories( qa_agent
    PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/agent
    ${CMAKE_SOURCE_DIR}/agent/core
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/thirdparty/nlohmann_json
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries( qa_agent
    common
    kicommon
    Boost::headers
    Boost::unit_test_framework
    ${wxWidgets_LIBRARIES}
)

target_compile_definitions( qa_agent
    PRIVATE
    BOOST_TEST_DYN_LINK
)

kicad_add_boost_test( qa_agent qa_agent )

setup_qa_env( qa_agent )
