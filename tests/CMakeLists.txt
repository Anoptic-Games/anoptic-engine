include(CTest)


# Testing for ``anoptic_time.h``
add_executable(anotest_time anotest_time.c)
target_include_directories(anotest_time PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_sources(anotest_time PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/time/time.c)
if (WIN32)
    target_sources(anotest_time PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/time/time_win64.c
    )
elseif (UNIX)
    target_sources(anotest_time PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/time/time_linux.c
    )
endif()
add_test(NAME TEST_anoptic_time COMMAND anotest_time)
set_tests_properties(TEST_anoptic_time PROPERTIES TIMEOUT 10)


# Testing for ``anoptic_logging.h``
# TODO: idk why this isn't linking.
add_executable(anotest_logging anotest_logging.c)
target_include_directories(anotest_logging PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
if (WIN32)
    target_sources(anotest_logging PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/time/time_win64.c
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/memory/memalign_win64.c
    )
elseif (UNIX)
    target_sources(anotest_logging PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/time/time_linux.c
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/time/memalign_linux.c
    )
endif()
target_sources(anotest_logging PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/logging/logging_core.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/threads/threads.c
)
add_test(NAME TEST_anoptic_logging COMMAND anotest_logging)
set_tests_properties(TEST_anoptic_logging PROPERTIES TIMEOUT 10)