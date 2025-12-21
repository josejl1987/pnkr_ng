add_executable(pnkr_rhi_pbr main.cpp)

target_compile_features(pnkr_rhi_pbr PRIVATE cxx_std_20)

target_link_libraries(pnkr_rhi_pbr PRIVATE pnkr_engine)

# Include the common sample utilities
target_include_directories(pnkr_rhi_pbr PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../common")

if(MSVC)
  target_compile_options(pnkr_rhi_pbr PRIVATE /W4)
else()
  target_compile_options(pnkr_rhi_pbr PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_rhi_pbr PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- Shaders: Compile and generate C++ structs ---
# This matches the style of pnkr_rhi_unlit
add_shader_target("shaders/pbr.vert")
add_dependencies(pnkr_rhi_pbr ${LAST_GENERATED_TARGET})

add_shader_target("shaders/pbr.frag")
add_dependencies(pnkr_rhi_pbr ${LAST_GENERATED_TARGET})

# Allow the code to find the generated headers (pbr.vert.h, etc.)
target_include_directories(pnkr_rhi_pbr PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# --- Assets and Binary setup ---
# --- Assets and Binary setup ---
add_custom_command(TARGET pnkr_rhi_pbr POST_BUILD
        # 1. Create directories
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_pbr>/shaders"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_pbr>/assets"

        # 2. Copy compiled shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:pnkr_rhi_pbr>/shaders"

        # 3. Copy assets (Assuming your source assets are in ${CMAKE_CURRENT_SOURCE_DIR}/assets)
        # Check if source assets folder exists to avoid build errors if empty
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:pnkr_rhi_pbr>/assets"
)
