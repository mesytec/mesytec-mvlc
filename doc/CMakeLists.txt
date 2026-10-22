find_package(Doxygen)
find_package(Sphinx)

if (NOT DOXYGEN_FOUND)
    message(STATUS "doxygen not found, documentation will not be built")
    return()
endif()

if (NOT SPHINX_EXECUTABLE)
    message(STATUS "sphinx not found, documentation will not be built")
    return()
endif()

# Check that the python interpreter running sphinx-build can import the required
# modules. The interpreter is taken from the sphinx-build shebang line, falling
# back to the system python3.
file(STRINGS ${SPHINX_EXECUTABLE} SPHINX_SHEBANG LIMIT_COUNT 1 REGEX "^#!")

if (SPHINX_SHEBANG)
    string(REGEX REPLACE "^#![ \t]*" "" SPHINX_PYTHON "${SPHINX_SHEBANG}")
    separate_arguments(SPHINX_PYTHON)
else()
    find_package(Python3 COMPONENTS Interpreter)
    set(SPHINX_PYTHON ${Python3_EXECUTABLE})
endif()

if (SPHINX_PYTHON)
    execute_process(
        COMMAND ${SPHINX_PYTHON} -c "import breathe, sphinx_bootstrap_theme"
        RESULT_VARIABLE SPHINX_MODULES_RESULT
        OUTPUT_QUIET ERROR_QUIET)
endif()

if (NOT SPHINX_PYTHON OR NOT SPHINX_MODULES_RESULT EQUAL 0)
    message(STATUS "sphinx python modules 'breathe' and/or 'sphinx_bootstrap_theme' not found, documentation will not be built")
    return()
endif()

file(REAL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../src DOXYGEN_STRIP_FROM_PATH)

configure_file(Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
configure_file(source/conf.py.in ${CMAKE_CURRENT_BINARY_DIR}/conf.py)

add_custom_target(mesytec_mvlc_doc_doxygen
    COMMAND Doxygen::doxygen ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    COMMENT "Running doxygen")

add_custom_target(mesytec_mvlc_doc_sphinx
    COMMAND ${SPHINX_EXECUTABLE}
        -E -b html
        -c ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/source
        ${CMAKE_CURRENT_BINARY_DIR}/html
    COMMENT "Building documentation with Sphinx"
)

add_dependencies(mesytec_mvlc_doc_sphinx mesytec_mvlc_doc_doxygen)
add_custom_target(mesytec_mvlc_doc ALL)
add_dependencies(mesytec_mvlc_doc mesytec_mvlc_doc_sphinx)

install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/html/ DESTINATION doc/mesytec-mvlc)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/doxygen/html/ DESTINATION doc/mesytec-mvlc/doxygen)
