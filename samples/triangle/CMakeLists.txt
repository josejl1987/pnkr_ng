add_executable(pnkr_triangle main.cpp)

target_compile_features(pnkr_triangle PRIVATE cxx_std_20)

target_link_libraries(pnkr_triangle PRIVATE pnkr_engine)

if(MSVC)
  target_compile_options(pnkr_triangle PRIVATE /W4)
else()
  target_compile_options(pnkr_triangle PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_triangle PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)


find_program(GLSLC_EXECUTABLE glslc HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "$ENV{VULKAN_SDK}/Bin32"
)
if (NOT GLSLC_EXECUTABLE)
  message(FATAL_ERROR "glslc not found. Install Vulkan SDK and ensure VULKAN_SDK is set.")
endif()


set(TRIANGLE_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(TRIANGLE_SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

file(MAKE_DIRECTORY "${TRIANGLE_SHADER_OUT_DIR}")

set(TRIANGLE_SHADERS
        "${TRIANGLE_SHADER_DIR}/triangle.vert"
        "${TRIANGLE_SHADER_DIR}/triangle.frag"
)

set(TRIANGLE_SPV_OUTPUTS "")
foreach(SHADER ${TRIANGLE_SHADERS})
  get_filename_component(FILE ${SHADER} NAME)
  set(OUT_SPV "${TRIANGLE_SHADER_OUT_DIR}/${FILE}.spv")

  add_custom_command(
          OUTPUT "${OUT_SPV}"
          COMMAND "${GLSLC_EXECUTABLE}" -o "${OUT_SPV}" "${SHADER}"
          DEPENDS "${SHADER}"
          COMMENT "Compiling shader ${FILE} -> ${FILE}.spv"
          VERBATIM
  )

  list(APPEND TRIANGLE_SPV_OUTPUTS "${OUT_SPV}")
endforeach()

add_custom_target(triangle_shaders ALL DEPENDS ${TRIANGLE_SPV_OUTPUTS})
add_dependencies(pnkr_triangle triangle_shaders)

add_custom_command(TARGET pnkr_triangle POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_triangle>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${TRIANGLE_SHADER_OUT_DIR}"
        "$<TARGET_FILE_DIR:pnkr_triangle>/shaders"
        COMMENT "Copying SPIR-V shaders to runtime output directory"
)