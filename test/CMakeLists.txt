add_executable(vt_test vt_test.cc)
target_link_libraries(vt_test GTest::gtest_main cmake_base_compiler_options nihvt nihgfx)
target_include_directories(vt_test PUBLIC "${PROJECT_SOURCE_DIR}/include")

include(GoogleTest)
gtest_discover_tests(vt_test WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
