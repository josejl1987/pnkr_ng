add_executable(pnkr_rhi_multidraw main.cpp)

target_compile_features(pnkr_rhi_multidraw PRIVATE cxx_std_20)

target_link_libraries(pnkr_rhi_multidraw PRIVATE pnkr_engine)

if(MSVC)
  target_compile_options(pnkr_rhi_multidraw PRIVATE /W4)
else()
  target_compile_options(pnkr_rhi_multidraw PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_rhi_multidraw PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Compile Fragment Shader
add_shader_target("shaders/cube.frag")
add_dependencies(pnkr_rhi_multidraw ${LAST_GENERATED_TARGET})

# Compile Vertex Shader
add_shader_target("shaders/cube.vert")
add_dependencies(pnkr_rhi_multidraw ${LAST_GENERATED_TARGET})

# Compile Plane Tint Shader (referenced from multidraw folder)
add_shader_target("../multidraw/shaders/plane_tint.frag")
add_dependencies(pnkr_rhi_multidraw ${LAST_GENERATED_TARGET})

target_include_directories(pnkr_rhi_multidraw PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

set_source_files_properties(main.cpp PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/generated/cube.vert.h"
)

add_custom_command(TARGET pnkr_rhi_multidraw POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_multidraw>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:pnkr_rhi_multidraw>/shaders"
        COMMENT "Copying SPIR-V shaders to runtime output directory"
)