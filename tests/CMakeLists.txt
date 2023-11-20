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
