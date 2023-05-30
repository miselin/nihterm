add_executable(vt_test vt_test.cc)
target_link_libraries(vt_test GTest::gtest_main cmake_base_compiler_options nihvt)
target_include_directories(vt_test PUBLIC "${PROJECT_SOURCE_DIR}/include")

add_executable(cord_test cord_test.cc)
target_link_libraries(cord_test GTest::gtest_main cmake_base_compiler_options cord)
target_include_directories(cord_test PUBLIC "${PROJECT_SOURCE_DIR}/include")

include(GoogleTest)
gtest_discover_tests(vt_test WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
gtest_discover_tests(cord_test WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
