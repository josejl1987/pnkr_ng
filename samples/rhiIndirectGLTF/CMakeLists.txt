add_executable(pnkr_rhi_indirect_gltf
    main.cpp
    IndirectRenderer.cpp
    IndirectRenderer.hpp
)

target_compile_features(pnkr_rhi_indirect_gltf PRIVATE cxx_std_20)
target_link_libraries(pnkr_rhi_indirect_gltf PRIVATE pnkr_engine)

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
foreach(SHADER ${INDIRECT_SHADERS})
  get_filename_component(FILE ${SHADER} NAME)
  set(OUT_SPV "${INDIRECT_SHADER_OUT_DIR}/${FILE}.spv")

  add_custom_command(
          OUTPUT "${OUT_SPV}"
          COMMAND "${GLSLC_EXECUTABLE}" -o "${OUT_SPV}" "${SHADER}" --target-env=vulkan1.2
          DEPENDS "${SHADER}"
          COMMENT "Compiling shader ${FILE} -> ${FILE}.spv"
          VERBATIM
  )

  list(APPEND INDIRECT_SPV_OUTPUTS "${OUT_SPV}")
endforeach()

add_custom_target(rhi_indirect_shaders ALL DEPENDS ${INDIRECT_SPV_OUTPUTS})
add_dependencies(pnkr_rhi_indirect_gltf rhi_indirect_shaders)

add_custom_command(TARGET pnkr_rhi_indirect_gltf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_indirect_gltf>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${INDIRECT_SHADER_OUT_DIR}"
        "$<TARGET_FILE_DIR:pnkr_rhi_indirect_gltf>/shaders"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_indirect_gltf>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/samples/rhiUnifiedMesh/assets/Bistro.glb"
        "$<TARGET_FILE_DIR:pnkr_rhi_indirect_gltf>/assets/Bistro.glb"
        COMMENT "Copying SPIR-V shaders and assets to runtime output directory"
)
