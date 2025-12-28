add_executable(pnkr_rhi_offscreen_mip_rendering main.cpp)

target_compile_features(pnkr_rhi_offscreen_mip_rendering PRIVATE cxx_std_20)

target_link_libraries(pnkr_rhi_offscreen_mip_rendering PRIVATE 
    pnkr_engine
)

if(MSVC)
  target_compile_options(pnkr_rhi_offscreen_mip_rendering PRIVATE /W4)
else()
  target_compile_options(pnkr_rhi_offscreen_mip_rendering PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_rhi_offscreen_mip_rendering PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)


add_shader_target("shaders/offscreen.vert")
add_dependencies(pnkr_rhi_offscreen_mip_rendering ${LAST_GENERATED_TARGET})

add_shader_target("shaders/offscreen.frag")
add_dependencies(pnkr_rhi_offscreen_mip_rendering ${LAST_GENERATED_TARGET})

add_custom_command(TARGET pnkr_rhi_offscreen_mip_rendering POST_BUILD
        # 1. Create directories
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_offscreen_mip_rendering>/shaders"

        # 2. Copy compiled shaders from engine (or they are already there if shared)
        # The engine already copies its shaders to bin/shaders.
        # We just need our local assets.
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:pnkr_rhi_offscreen_mip_rendering>/shaders"

)