add_executable(pnkr_rhi_indirect_gltf
    main.cpp
)

target_compile_features(pnkr_rhi_indirect_gltf PRIVATE cxx_std_20)

target_include_directories(pnkr_rhi_indirect_gltf PRIVATE
    "${CMAKE_SOURCE_DIR}/engine/thirdparty/renderdoc"
)

find_package(nfd CONFIG REQUIRED)

target_link_libraries(pnkr_rhi_indirect_gltf PRIVATE 
    pnkr_engine
    nfd::nfd
)

if(MSVC)
  target_compile_options(pnkr_rhi_indirect_gltf PRIVATE /W4)
else()
  target_compile_options(pnkr_rhi_indirect_gltf PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_rhi_indirect_gltf PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- Assets and Binary setup ---
find_file(RENDERDOC_DLL "renderdoc.dll" HINTS "C:/Program Files/RenderDoc")

add_custom_command(TARGET pnkr_rhi_indirect_gltf POST_BUILD
# 1. Create directories
COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_indirect_gltf>/shaders"
COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_indirect_gltf>/assets"

# 2. Copy compiled shaders from engine (or they are already there if shared)
# The engine already copies its shaders to bin/shaders. 
# We just need our local assets.
COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:pnkr_rhi_indirect_gltf>/assets"
)

if (RENDERDOC_DLL)
    add_custom_command(TARGET pnkr_rhi_indirect_gltf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Deploying RenderDoc DLL from: ${RENDERDOC_DLL}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RENDERDOC_DLL}" "$<TARGET_FILE_DIR:pnkr_rhi_indirect_gltf>/renderdoc.dll"
    )
endif()