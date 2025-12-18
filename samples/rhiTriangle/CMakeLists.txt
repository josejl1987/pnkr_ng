add_executable(pnkr_rhi_triangle main.cpp)

target_compile_features(pnkr_rhi_triangle PRIVATE cxx_std_20)

target_link_libraries(pnkr_rhi_triangle PRIVATE pnkr_engine)

if(MSVC)
  target_compile_options(pnkr_rhi_triangle PRIVATE /W4)
else()
  target_compile_options(pnkr_rhi_triangle PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_rhi_triangle PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)


find_program(GLSLC_EXECUTABLE glslc HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "$ENV{VULKAN_SDK}/Bin32"
)
if (NOT GLSLC_EXECUTABLE)
  message(FATAL_ERROR "glslc not found. Install Vulkan SDK and ensure VULKAN_SDK is set.")
endif()


set(RHI_TRIANGLE_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(RHI_TRIANGLE_SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

file(MAKE_DIRECTORY "${RHI_TRIANGLE_SHADER_OUT_DIR}")

set(RHI_TRIANGLE_SHADERS
        "${RHI_TRIANGLE_SHADER_DIR}/triangle.vert"
        "${RHI_TRIANGLE_SHADER_DIR}/triangle.frag"
)

set(RHI_TRIANGLE_SPV_OUTPUTS "")
foreach(SHADER ${RHI_TRIANGLE_SHADERS})
  get_filename_component(FILE ${SHADER} NAME)
  set(OUT_SPV "${RHI_TRIANGLE_SHADER_OUT_DIR}/${FILE}.spv")

  add_custom_command(
          OUTPUT "${OUT_SPV}"
          COMMAND "${GLSLC_EXECUTABLE}" -o "${OUT_SPV}" "${SHADER}"
          DEPENDS "${SHADER}"
          COMMENT "Compiling shader ${FILE} -> ${FILE}.spv"
          VERBATIM
  )

  list(APPEND RHI_TRIANGLE_SPV_OUTPUTS "${OUT_SPV}")
endforeach()

add_custom_target(rhi_triangle_shaders ALL DEPENDS ${RHI_TRIANGLE_SPV_OUTPUTS})
add_dependencies(pnkr_rhi_triangle rhi_triangle_shaders)

add_custom_command(TARGET pnkr_rhi_triangle POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_triangle>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${RHI_TRIANGLE_SHADER_OUT_DIR}"
        "$<TARGET_FILE_DIR:pnkr_rhi_triangle>/shaders"
        COMMENT "Copying SPIR-V shaders to runtime output directory"
)