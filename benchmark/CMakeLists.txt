project(benchmarks)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)

macro (ADD_BENCHMARK BENCHMARK_NAME)
add_executable(${BENCHMARK_NAME} "${BENCHMARK_NAME}.cc")
target_link_libraries(${BENCHMARK_NAME} benchmark nihvt nihgfx cmake_base_compiler_options "${CMAKE_THREAD_LIBS_INIT}")
endmacro ()

ADD_BENCHMARK(bench-scroll)
