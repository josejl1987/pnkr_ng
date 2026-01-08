add_executable(pnkr_rhi_cube main.cpp)

target_compile_features(pnkr_rhi_cube PRIVATE cxx_std_20)

target_link_libraries(pnkr_rhi_cube PRIVATE pnkr_engine)

if(MSVC)
  target_compile_options(pnkr_rhi_cube PRIVATE /W4)
else()
  target_compile_options(pnkr_rhi_cube PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_rhi_cube PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Compile Fragment Shader
add_shader_target("shaders/cube.frag")
add_dependencies(pnkr_rhi_cube ${LAST_GENERATED_TARGET})

# Compile Vertex Shader
add_shader_target("shaders/cube.vert")
add_dependencies(pnkr_rhi_cube ${LAST_GENERATED_TARGET})

target_include_directories(pnkr_rhi_cube PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

add_custom_command(TARGET pnkr_rhi_cube POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_cube>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:pnkr_rhi_cube>/shaders"
        COMMENT "Copying SPIR-V shaders to runtime output directory"
)