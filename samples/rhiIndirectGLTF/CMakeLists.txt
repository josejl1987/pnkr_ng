add_executable(pnkr_rhi_indirect_gltf
    main.cpp
    IndirectRenderer.cpp
    IndirectRenderer.hpp
    RecentFiles.cpp
    RecentFiles.hpp
    RenderDocIntegration.cpp
    RenderDocIntegration.hpp
    ../common/NfdFileDialog.cpp
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

find_program(GLSLC_EXECUTABLE glslc HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "$ENV{VULKAN_SDK}/Bin32"
)
if (NOT GLSLC_EXECUTABLE)
  message(FATAL_ERROR "glslc not found. Install Vulkan SDK and ensure VULKAN_SDK is set.")
endif()

set(INDIRECT_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(INDIRECT_SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

file(MAKE_DIRECTORY "${INDIRECT_SHADER_OUT_DIR}")

set(INDIRECT_SHADERS
        "${INDIRECT_SHADER_DIR}/indirect.vert"
        "${INDIRECT_SHADER_DIR}/indirect.frag"
)

set(INDIRECT_SPV_OUTPUTS "")
# --- Shaders: Compile and generate C++ structs ---
add_shader_target("shaders/indirect.vert")
add_dependencies(pnkr_rhi_indirect_gltf ${LAST_GENERATED_TARGET})

add_shader_target("shaders/indirect.frag")
add_dependencies(pnkr_rhi_indirect_gltf ${LAST_GENERATED_TARGET})

add_shader_target("shaders/skinning.comp")
add_dependencies(pnkr_rhi_indirect_gltf ${LAST_GENERATED_TARGET})

# Allow the code to find the generated headers (pbr.vert.h, etc.)
target_include_directories(pnkr_rhi_indirect_gltf PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# --- Assets and Binary setup ---
find_file(RENDERDOC_DLL "renderdoc.dll" HINTS "C:/Program Files/RenderDoc")

add_custom_command(TARGET pnkr_rhi_indirect_gltf POST_BUILD
# 1. Create directories
COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_indirect_gltf>/shaders"
COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_indirect_gltf>/assets"

# 2. Copy compiled shaders
COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:pnkr_rhi_indirect_gltf>/shaders"
COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:pnkr_rhi_indirect_gltf>/assets"

# 3. Deploy RenderDoc DLL if found
COMMAND ${CMAKE_COMMAND} -E echo "Deploying RenderDoc DLL from: ${RENDERDOC_DLL}"
COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RENDERDOC_DLL}" "$<TARGET_FILE_DIR:pnkr_rhi_indirect_gltf>/renderdoc.dll"
)
