add_executable(pnkr_rhi_computed_mesh main.cpp)

target_compile_features(pnkr_rhi_computed_mesh PRIVATE cxx_std_20)

target_link_libraries(pnkr_rhi_computed_mesh PRIVATE pnkr_engine)

if(MSVC)
  target_compile_options(pnkr_rhi_computed_mesh PRIVATE /W4)
else()
  target_compile_options(pnkr_rhi_computed_mesh PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_rhi_computed_mesh PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- Shaders ---
set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

# Compute Shaders
add_shader_target("shaders/mesh.comp")
add_dependencies(pnkr_rhi_computed_mesh ${LAST_GENERATED_TARGET})

add_shader_target("shaders/texture.comp")
add_dependencies(pnkr_rhi_computed_mesh ${LAST_GENERATED_TARGET})

# Graphics Shaders
add_shader_target("shaders/mesh.vert")
add_dependencies(pnkr_rhi_computed_mesh ${LAST_GENERATED_TARGET})

add_shader_target("shaders/mesh.geom")
add_dependencies(pnkr_rhi_computed_mesh ${LAST_GENERATED_TARGET})

add_shader_target("shaders/mesh.frag")
add_dependencies(pnkr_rhi_computed_mesh ${LAST_GENERATED_TARGET})

target_include_directories(pnkr_rhi_computed_mesh PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Copy logic
add_custom_command(TARGET pnkr_rhi_computed_mesh POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_computed_mesh>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:pnkr_rhi_computed_mesh>/shaders"
)
