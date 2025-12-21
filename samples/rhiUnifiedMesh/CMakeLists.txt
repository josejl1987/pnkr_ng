add_executable(pnkr_rhi_unified_mesh main.cpp)

target_compile_features(pnkr_rhi_unified_mesh PRIVATE cxx_std_20)



target_link_libraries(pnkr_rhi_unified_mesh 
    PRIVATE 
    pnkr_engine 
)

if(MSVC)
    target_compile_options(pnkr_rhi_unified_mesh PRIVATE /W4)
else()
    target_compile_options(pnkr_rhi_unified_mesh PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_rhi_unified_mesh PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Shaders
add_shader_target("shaders/unified.vert")
add_dependencies(pnkr_rhi_unified_mesh ${LAST_GENERATED_TARGET})

add_shader_target("shaders/unified.frag")
add_dependencies(pnkr_rhi_unified_mesh ${LAST_GENERATED_TARGET})

add_shader_target("shaders/unified.geom")
add_dependencies(pnkr_rhi_unified_mesh ${LAST_GENERATED_TARGET})

target_include_directories(pnkr_rhi_unified_mesh PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

set_source_files_properties(main.cpp PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/generated/unified.vert.h;${CMAKE_CURRENT_BINARY_DIR}/generated/unified.geom.h;${CMAKE_CURRENT_BINARY_DIR}/generated/unified.frag.h"
)

# Copy logic
add_custom_command(TARGET pnkr_rhi_unified_mesh POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_unified_mesh>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:pnkr_rhi_unified_mesh>/shaders"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_unified_mesh>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/samples/rhiUnifiedMesh/assets"
        "$<TARGET_FILE_DIR:pnkr_rhi_unified_mesh>/assets"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_unified_mesh>/assets/skybox"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/samples/rhiSkybox/assets/skybox"
        "$<TARGET_FILE_DIR:pnkr_rhi_unified_mesh>/assets/skybox"
        COMMENT "Copying assets and shaders"
)
