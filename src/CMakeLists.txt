add_library(nihgfx "gfx.c")
target_link_libraries(nihgfx PUBLIC cmake_base_compiler_options ${SDL2_LIBRARIES} ${SDL2_TTF_LIBRARY} ${GTK2_LIBRARIES} Fontconfig::Fontconfig)
target_include_directories(nihgfx PUBLIC "${PROJECT_SOURCE_DIR}/include" ${GTK2_INCLUDE_DIRS})

add_library(nihvt "vt.c")
target_link_libraries(nihvt PUBLIC cmake_base_compiler_options)
target_include_directories(nihvt PUBLIC "${PROJECT_SOURCE_DIR}/include")

add_executable(nihterm "main.c")
target_link_libraries(nihterm PRIVATE cmake_base_compiler_options nihvt nihgfx)
target_include_directories(nihterm PUBLIC "${PROJECT_SOURCE_DIR}/include")

add_executable(decawm "decawm.c")
target_link_libraries(decawm PRIVATE cmake_base_compiler_options nihvt nihgfx)
target_include_directories(decawm PUBLIC "${PROJECT_SOURCE_DIR}/include")