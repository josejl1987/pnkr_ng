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

# --- Shaders ---
# Uses ShaderTools.cmake to compile and generate C++ structs
add_shader_target("shaders/skybox.vert")
add_dependencies(pnkr_skybox ${LAST_GENERATED_TARGET})

add_shader_target("shaders/skybox.frag")
add_dependencies(pnkr_skybox ${LAST_GENERATED_TARGET})

# Include generated headers
target_include_directories(pnkr_skybox PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Rebuild if shader headers change
set_source_files_properties(main.cpp PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/generated/skybox.vert.h"
)

# --- Copy Assets ---
add_custom_command(TARGET pnkr_skybox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_skybox>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:pnkr_skybox>/shaders"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_skybox>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:pnkr_skybox>/assets"
        COMMENT "Copying assets and shaders"
)