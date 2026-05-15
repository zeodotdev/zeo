# Freerouting autorouter build configuration
# Builds the Freerouting executable JAR using gradle

include(ExternalProject)

set(FREEROUTING_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../../../tools/freerouting")
set(FREEROUTING_JAR "${FREEROUTING_SOURCE_DIR}/build/libs/freerouting-executable.jar")

# Build the executable JAR using gradle
# The JAR is built in-source and we just need to run the gradle task
ExternalProject_Add(
    freerouting
    PREFIX ${CMAKE_BINARY_DIR}/freerouting
    SOURCE_DIR ${FREEROUTING_SOURCE_DIR}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND cd ${FREEROUTING_SOURCE_DIR} && ./gradlew executableJar --no-daemon
    BUILD_IN_SOURCE 1
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${FREEROUTING_JAR}
)

# Export variables for use in kicad.cmake
set(freerouting_JAR_PATH ${FREEROUTING_JAR} PARENT_SCOPE)
set(freerouting_SOURCE_DIR ${FREEROUTING_SOURCE_DIR} PARENT_SCOPE)
