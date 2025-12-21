add_executable(pnkr_skybox main.cpp)

target_compile_features(pnkr_skybox PRIVATE cxx_std_20)

# Dependencies
find_package(fastgltf CONFIG REQUIRED)
target_link_libraries(pnkr_skybox PRIVATE pnkr_engine fastgltf::fastgltf)

if(MSVC)
    target_compile_options(pnkr_skybox PRIVATE /W4)
else()
    target_compile_options(pnkr_skybox PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_skybox PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)



# --- Copy Assets ---
add_custom_command(TARGET pnkr_skybox POST_BUILD

        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_skybox>/assets"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_skybox>/assets/skybox"

        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:pnkr_skybox>/assets"
        COMMENT "Copying assets and shaders"
)