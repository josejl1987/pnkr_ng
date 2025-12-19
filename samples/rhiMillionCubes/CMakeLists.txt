add_executable(pnkr_million_cubes main.cpp)

target_compile_features(pnkr_million_cubes PRIVATE cxx_std_20)

# Dependencies
find_package(fastgltf CONFIG REQUIRED)
target_link_libraries(pnkr_million_cubes PRIVATE pnkr_engine fastgltf::fastgltf)

if(MSVC)
  target_compile_options(pnkr_million_cubes PRIVATE /W4)
else()
  target_compile_options(pnkr_million_cubes PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_million_cubes PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- Shaders ---
# Uses ShaderTools.cmake to compile and generate C++ structs
add_shader_target("shaders/million_cubes.vert")
add_dependencies(pnkr_million_cubes ${LAST_GENERATED_TARGET})

add_shader_target("shaders/million_cubes.frag")
add_dependencies(pnkr_million_cubes ${LAST_GENERATED_TARGET})

# Include generated headers
target_include_directories(pnkr_million_cubes PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Rebuild if shader headers change
set_source_files_properties(main.cpp PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/generated/million_cubes.vert.h;${CMAKE_CURRENT_BINARY_DIR}/generated/million_cubes.frag.h"
)

# --- Copy Assets ---
add_custom_command(TARGET pnkr_million_cubes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_million_cubes>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:pnkr_million_cubes>/shaders"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_million_cubes>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:pnkr_million_cubes>/assets"
        COMMENT "Copying assets and shaders"
)