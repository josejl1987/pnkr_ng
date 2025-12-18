add_executable(pnkr_cube main.cpp
        ../common/RhiSampleApp.hpp)

target_compile_features(pnkr_cube PRIVATE cxx_std_20)

target_link_libraries(pnkr_cube PRIVATE pnkr_engine)

if(MSVC)
  target_compile_options(pnkr_cube PRIVATE /W4)
else()
  target_compile_options(pnkr_cube PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_cube PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)


find_program(GLSLC_EXECUTABLE glslc HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "$ENV{VULKAN_SDK}/Bin32"
)
if (NOT GLSLC_EXECUTABLE)
  message(FATAL_ERROR "glslc not found. Install Vulkan SDK and ensure VULKAN_SDK is set.")
endif()


set(CUBE_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(CUBE_SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

file(MAKE_DIRECTORY "${CUBE_SHADER_OUT_DIR}")

set(CUBE_SHADERS
        "${CUBE_SHADER_DIR}/cube.vert"
        "${CUBE_SHADER_DIR}/cube.frag"
)

set(CUBE_SPV_OUTPUTS "")
foreach(SHADER ${CUBE_SHADERS})
  get_filename_component(FILE ${SHADER} NAME)
  set(OUT_SPV "${CUBE_SHADER_OUT_DIR}/${FILE}.spv")

  add_custom_command(
          OUTPUT "${OUT_SPV}"
          COMMAND "${GLSLC_EXECUTABLE}" -o "${OUT_SPV}" "${SHADER}"
          DEPENDS "${SHADER}"
          COMMENT "Compiling shader ${FILE} -> ${FILE}.spv"
          VERBATIM
  )

  list(APPEND CUBE_SPV_OUTPUTS "${OUT_SPV}")
endforeach()

add_custom_target(cube_shaders ALL DEPENDS ${CUBE_SPV_OUTPUTS})
add_dependencies(pnkr_cube cube_shaders)

add_custom_command(TARGET pnkr_cube POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_cube>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CUBE_SHADER_OUT_DIR}"
        "$<TARGET_FILE_DIR:pnkr_cube>/shaders"
        COMMENT "Copying SPIR-V shaders to runtime output directory"
)