add_executable(pnkr_rhi_unified_gltf_dod main.cpp)

target_compile_features(pnkr_rhi_unified_gltf_dod PRIVATE cxx_std_20)

target_link_libraries(pnkr_rhi_unified_gltf_dod PRIVATE pnkr_engine)

# Include the common sample utilities
target_include_directories(pnkr_rhi_unified_gltf_dod PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../common")

if(MSVC)
  target_compile_options(pnkr_rhi_unified_gltf_dod PRIVATE /W4)
else()
  target_compile_options(pnkr_rhi_unified_gltf_dod PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_rhi_unified_gltf_dod PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- Shaders: Compile and generate C++ structs ---
add_shader_target("shaders/gltf.vert")
add_dependencies(pnkr_rhi_unified_gltf_dod ${LAST_GENERATED_TARGET})

add_shader_target("shaders/gltf.frag")
add_dependencies(pnkr_rhi_unified_gltf_dod ${LAST_GENERATED_TARGET})

# Allow the code to find the generated headers (pbr.vert.h, etc.)
target_include_directories(pnkr_rhi_unified_gltf_dod PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# --- Assets and Binary setup ---
add_custom_command(TARGET pnkr_rhi_unified_gltf_dod POST_BUILD
        # 1. Create directories
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_unified_gltf_dod>/shaders"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_unified_gltf_dod>/assets"

        # 2. Copy compiled shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:pnkr_rhi_unified_gltf_dod>/shaders"

        # 3. Copy assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:pnkr_rhi_unified_gltf_dod>/assets"
)
