ExternalProject_Add(
    templates
    PREFIX  templates
    GIT_REPOSITORY ${TEMPLATES_URL}
    GIT_TAG ${TEMPLATES_TAG}
    CMAKE_ARGS "-DCMAKE_INSTALL_PREFIX=<BINARY_DIR>/output"
)

ExternalProject_Get_Property(templates BINARY_DIR)
set(templates_INSTALL_DIR ${BINARY_DIR}/output)
