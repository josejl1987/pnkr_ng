add_executable(pnkr_grid main.cpp)

target_compile_features(pnkr_grid PRIVATE cxx_std_20)

# Dependencies
find_package(fastgltf CONFIG REQUIRED)
target_link_libraries(pnkr_grid PRIVATE pnkr_engine fastgltf::fastgltf)

if(MSVC)
    target_compile_options(pnkr_grid PRIVATE /W4)
else()
    target_compile_options(pnkr_grid PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_grid PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)



# --- Copy Assets ---
add_custom_command(TARGET pnkr_grid POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_grid>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:pnkr_grid>/assets"
        COMMENT "Copying assets and shaders"
)