add_executable(pnkr_rhi_brdf_lut main.cpp)
target_link_libraries(pnkr_rhi_brdf_lut PRIVATE pnkr_engine)

# Add shader and generate header
add_shader_target("shaders/brdf_lut.comp")
add_dependencies(pnkr_rhi_brdf_lut ${LAST_GENERATED_TARGET})

target_include_directories(pnkr_rhi_brdf_lut PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Copy shaders to output
add_custom_command(TARGET pnkr_rhi_brdf_lut POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_brdf_lut>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:pnkr_rhi_brdf_lut>/shaders"
)
