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
if(WIN32)
    # Attempt to find RenderDoc installation
    find_file(RENDERDOC_DLL "renderdoc.dll" 
        HINTS 
            "$ENV{ProgramFiles}/RenderDoc"
            "C:/Program Files/RenderDoc"
        NO_CMAKE_FIND_ROOT_PATH
    )

    if (EXISTS "${RENDERDOC_DLL}")
        message(STATUS "Found RenderDoc at: ${RENDERDOC_DLL}")
        
        # Copy to the directory where the exe is built
        add_custom_command(TARGET pnkr_rhi_indirect_gltf POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            "${RENDERDOC_DLL}" 
            "$<TARGET_FILE_DIR:pnkr_rhi_indirect_gltf>/renderdoc.dll"
            COMMENT "Copying renderdoc.dll to output bin directory"
        )
    else()
        message(WARNING "RenderDoc DLL not found! You must copy 'renderdoc.dll' to the bin folder manually for F10 to work.")
    endif()
endif()

add_custom_command(TARGET pnkr_rhi_indirect_gltf POST_BUILD
# 1. Create directories
COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_indirect_gltf>/shaders"
COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_indirect_gltf>/assets"

# 2. Copy compiled shaders from engine (or they are already there if shared)
# The engine already copies its shaders to bin/shaders. 
# We just need our local assets.
COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:pnkr_rhi_indirect_gltf>/assets"
)