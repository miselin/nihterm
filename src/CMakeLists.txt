add_library(nihvt "gfx.c" "vt.c")
target_link_libraries(nihvt PUBLIC cmake_base_compiler_options ${SDL2_LIBRARIES} ${SDL2_TTF_LIBRARY})
target_include_directories(nihvt PUBLIC "${PROJECT_SOURCE_DIR}/include")

add_library(cord "cord.c")
target_link_libraries(cord PUBLIC cmake_base_compiler_options)
target_include_directories(cord PUBLIC "${PROJECT_SOURCE_DIR}/include")

add_executable(nihterm "main.c")
target_link_libraries(nihterm PRIVATE cmake_base_compiler_options nihvt cord)
target_include_directories(nihterm PUBLIC "${PROJECT_SOURCE_DIR}/include")

add_executable(decawm "decawm.c")
target_link_libraries(decawm PRIVATE cmake_base_compiler_options nihvt)
target_include_directories(decawm PUBLIC "${PROJECT_SOURCE_DIR}/include")