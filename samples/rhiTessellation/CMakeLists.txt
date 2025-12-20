add_executable(pnkr_rhi_tessellation main.cpp)

target_compile_features(pnkr_rhi_tessellation PRIVATE cxx_std_20)

# Dependencies
find_package(fastgltf CONFIG REQUIRED)
target_link_libraries(pnkr_rhi_tessellation PRIVATE pnkr_engine fastgltf::fastgltf)

if(MSVC)
  target_compile_options(pnkr_rhi_tessellation PRIVATE /W4)
else()
  target_compile_options(pnkr_rhi_tessellation PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_rhi_tessellation PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- Shaders ---
# Uses ShaderTools.cmake to compile and generate C++ structs
add_shader_target("shaders/tessellation.vert")
add_dependencies(pnkr_rhi_tessellation ${LAST_GENERATED_TARGET})

add_shader_target("shaders/tessellation.tesc")
add_dependencies(pnkr_rhi_tessellation ${LAST_GENERATED_TARGET})

add_shader_target("shaders/tessellation.tese")
add_dependencies(pnkr_rhi_tessellation ${LAST_GENERATED_TARGET})

add_shader_target("shaders/wireframe.geom")
add_dependencies(pnkr_rhi_tessellation ${LAST_GENERATED_TARGET})

add_shader_target("shaders/gltf_bindless.frag")
add_dependencies(pnkr_rhi_tessellation ${LAST_GENERATED_TARGET})

# Include generated headers
target_include_directories(pnkr_rhi_tessellation PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Rebuild if shader headers change
set_source_files_properties(main.cpp PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/generated/tessellation.vert.h;${CMAKE_CURRENT_BINARY_DIR}/generated/gltf_bindless.frag.h"
)

# --- Copy Assets ---
add_custom_command(TARGET pnkr_rhi_tessellation POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_tessellation>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:pnkr_rhi_tessellation>/shaders"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_tessellation>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:pnkr_rhi_tessellation>/assets"
        COMMENT "Copying assets and shaders"
)
