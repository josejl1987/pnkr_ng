# tools/CMakeLists.txt

add_executable(ShaderStructGen ShaderStructGen.cpp)

# --- CORRECT ORDER ---
find_package(spirv_cross_core CONFIG REQUIRED)
find_package(spirv_cross_glsl CONFIG REQUIRED)
find_package(spirv_cross_cpp CONFIG REQUIRED)

# Link all of them
target_link_libraries(ShaderStructGen PRIVATE
        spirv-cross-core
        spirv-cross-glsl
        spirv-cross-cpp
)

target_compile_features(ShaderStructGen PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(ShaderStructGen PRIVATE /MD$<$<CONFIG:Debug>:d>)
endif()
