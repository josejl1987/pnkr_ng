add_executable(pnkr_instanced_ducks main.cpp)

target_compile_features(pnkr_instanced_ducks PRIVATE cxx_std_20)

# Dependencies
find_package(fastgltf CONFIG REQUIRED)
target_link_libraries(pnkr_instanced_ducks PRIVATE pnkr_engine fastgltf::fastgltf)

if(MSVC)
  target_compile_options(pnkr_instanced_ducks PRIVATE /W4)
else()
  target_compile_options(pnkr_instanced_ducks PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_instanced_ducks PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- Shaders ---
# Uses ShaderTools.cmake to compile and generate C++ structs
add_shader_target("shaders/instanced_ducks_compute.comp")
add_dependencies(pnkr_instanced_ducks ${LAST_GENERATED_TARGET})

add_shader_target("shaders/instanced_ducks.vert")
add_dependencies(pnkr_instanced_ducks ${LAST_GENERATED_TARGET})

add_shader_target("shaders/instanced_ducks.frag")
add_dependencies(pnkr_instanced_ducks ${LAST_GENERATED_TARGET})

# Include generated headers
target_include_directories(pnkr_instanced_ducks PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Rebuild if shader headers change
set_source_files_properties(main.cpp PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/generated/instanced_ducks_compute.comp.h;${CMAKE_CURRENT_BINARY_DIR}/generated/instanced_ducks.vert.h;${CMAKE_CURRENT_BINARY_DIR}/generated/instanced_ducks.frag.h"
)

# --- Copy Assets ---
add_custom_command(TARGET pnkr_instanced_ducks POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_instanced_ducks>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:pnkr_instanced_ducks>/shaders"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_instanced_ducks>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:pnkr_instanced_ducks>/assets"
        COMMENT "Copying assets and shaders"
)