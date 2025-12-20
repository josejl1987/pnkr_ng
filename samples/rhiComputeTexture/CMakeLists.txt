add_executable(pnkr_rhi_compute_texture main.cpp)

target_compile_features(pnkr_rhi_compute_texture PRIVATE cxx_std_20)
target_link_libraries(pnkr_rhi_compute_texture PRIVATE pnkr_engine)

if(MSVC)
  target_compile_options(pnkr_rhi_compute_texture PRIVATE /W4)
else()
  target_compile_options(pnkr_rhi_compute_texture PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_rhi_compute_texture PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Shaders
add_shader_target("shaders/industrial.comp")
add_dependencies(pnkr_rhi_compute_texture ${LAST_GENERATED_TARGET})

add_shader_target("shaders/fullscreen.vert")
add_dependencies(pnkr_rhi_compute_texture ${LAST_GENERATED_TARGET})

add_shader_target("shaders/fullscreen.frag")
add_dependencies(pnkr_rhi_compute_texture ${LAST_GENERATED_TARGET})

target_include_directories(pnkr_rhi_compute_texture PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Copy logic
add_custom_command(TARGET pnkr_rhi_compute_texture POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_compute_texture>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:pnkr_rhi_compute_texture>/shaders"
)