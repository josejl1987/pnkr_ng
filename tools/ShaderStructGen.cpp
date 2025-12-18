// tools/ShaderStructGen.cpp
#include <spirv_cross.hpp>
#include <fstream>
#include <vector>
#include <iostream>
#include <string>
#include <algorithm>
#include <set>
#include <filesystem>

// -----------------------------------------------------------------------------
// Helper: Get C++ type name
// -----------------------------------------------------------------------------
std::string get_cpp_type_name(spirv_cross::Compiler& comp, const spirv_cross::SPIRType& type)
{
    if (type.basetype == spirv_cross::SPIRType::Struct)
    {
        std::string name = comp.get_name(type.self);
        if (name.empty())
        {
            return "Struct_" + std::to_string(type.self);
        }
        size_t dot = name.find_last_of('.');
        if (dot != std::string::npos)
        {
            return name.substr(dot + 1);
        }
        return name;
    }

    if (type.basetype == spirv_cross::SPIRType::Float)
    {
        if (type.columns == 4 && type.vecsize == 4) return "glm::mat4";
        if (type.columns == 3 && type.vecsize == 3) return "glm::mat3";
        if (type.vecsize == 4) return "glm::vec4";
        if (type.vecsize == 3) return "glm::vec3";
        if (type.vecsize == 2) return "glm::vec2";
        return "float";
    }

    if (type.basetype == spirv_cross::SPIRType::Int)
    {
        if (type.vecsize == 4) return "glm::ivec4";
        if (type.vecsize == 3) return "glm::ivec3";
        if (type.vecsize == 2) return "glm::ivec2";
        return "int32_t";
    }

    if (type.basetype == spirv_cross::SPIRType::UInt)
    {
        if (type.vecsize == 4) return "glm::uvec4";
        if (type.vecsize == 3) return "glm::uvec3";
        if (type.vecsize == 2) return "glm::uvec2";
        return "uint32_t";
    }

    if (type.basetype == spirv_cross::SPIRType::Boolean)
    {
        return "uint32_t /* bool */";
    }

    return "uint8_t"; // Fallback
}

// -----------------------------------------------------------------------------
// Core Generator Function
// -----------------------------------------------------------------------------
void generate_struct(spirv_cross::Compiler& comp, uint32_t type_id, std::ofstream& out,
                     std::set<uint32_t>& emittedTypes)
{
    if (emittedTypes.count(type_id)) return;
    emittedTypes.insert(type_id);

    auto& type = comp.get_type(type_id);

    // Recursively generate nested structs
    for (auto& member_type_id : type.member_types)
    {
        auto& member_type = comp.get_type(member_type_id);
        if (member_type.basetype == spirv_cross::SPIRType::Struct)
        {
            generate_struct(comp, member_type_id, out, emittedTypes);
        }
    }

    std::string structName = get_cpp_type_name(comp, type);
    size_t totalSize = comp.get_declared_struct_size(type);

    out << "// SPIR-V ID: " << type_id << " | Size: " << totalSize << " bytes\n";
    out << "struct " << structName << " {\n";

    struct Member
    {
        uint32_t index;
        uint32_t offset;
        uint32_t size;
        uint32_t matrixStride;
        uint32_t arrayStride;
        std::string name;
        spirv_cross::SPIRType type;
    };
    std::vector<Member> members;

    for (uint32_t i = 0; i < type.member_types.size(); ++i)
    {
        auto memberType = comp.get_type(type.member_types[i]);

        uint32_t arrStride = 0;
        if (!memberType.array.empty())
        {
            arrStride = comp.type_struct_member_array_stride(type, i);
        }

        uint32_t matStride = 0;
        if (memberType.columns > 1)
        {
            matStride = comp.type_struct_member_matrix_stride(type, i);
        }

        Member m = {
            i,
            comp.type_struct_member_offset(type, i),
            (uint32_t)comp.get_declared_struct_member_size(type, i),
            matStride,
            arrStride,
            comp.get_member_name(type.self, i),
            memberType
        };

        if (m.name.empty()) m.name = "member_" + std::to_string(i);
        members.push_back(m);
    }

    // Sort by memory offset
    std::sort(members.begin(), members.end(), [](const Member& a, const Member& b)
    {
        return a.offset < b.offset;
    });

    uint32_t currentOffset = 0;

    for (const auto& m : members)
    {
        // Padding
        if (m.offset > currentOffset)
        {
            out << "    uint8_t _pad_" << currentOffset << "[" << (m.offset - currentOffset) << "];\n";
        }

        std::string cppType = get_cpp_type_name(comp, m.type);

        // Output Field
        if (!m.type.array.empty())
        {
            uint32_t arraySize = m.type.array[0];
            if (m.arrayStride > 0 && arraySize > 1)
            {
                out << "    // Array Stride: " << m.arrayStride << "\n";
            }
            out << "    " << cppType << " " << m.name << "[" << arraySize << "];\n";
        }
        else
        {
            out << "    " << cppType << " " << m.name << ";\n";
        }

        currentOffset = m.offset + m.size;
    }

    // Tail Padding
    if (currentOffset < totalSize)
    {
        out << "    uint8_t _pad_end[" << (totalSize - currentOffset) << "];\n";
    }

    out << "};\n";
    if (totalSize == 0)
    {
        out << "// NOTE: Runtime-sized buffer (unsized array). Size check is not portable in C++.\n\n";
    }
    else
    {
        out << "static_assert(sizeof(" << structName << ") == " << totalSize
            << ", \"Size mismatch for " << structName << "\");\n\n";
    }
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    // Basic Argument Check
    if (argc < 3)
    {
        std::cerr << "Usage: ShaderStructGen <input.spv> <output.h>\n";
        return 1;
    }

    std::cout << "[ShaderStructGen] Processing: " << argv[1] << std::endl;

    // 1. Robust File Loading
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        std::cerr << "Error: Failed to open input file: " << argv[1] << "\n";
        return 1;
    }

    std::streamsize fileSize = file.tellg();
    if (fileSize <= 0)
    {
        std::cerr << "Error: Input file is empty or invalid.\n";
        return 1;
    }
    if (fileSize % 4 != 0)
    {
        std::cerr << "Error: File size (" << fileSize << ") is not aligned to 4 bytes (invalid SPIR-V).\n";
        return 1;
    }

    file.seekg(0, std::ios::beg);
    std::vector<uint32_t> spirv_binary(fileSize / 4);
    if (!file.read((char*)spirv_binary.data(), fileSize))
    {
        std::cerr << "Error: Failed to read file content.\n";
        return 1;
    }

    // 2. Initialize Compiler
    spirv_cross::Compiler comp(spirv_binary);
    spirv_cross::ShaderResources resources;
    try
    {
        resources = comp.get_shader_resources();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: Failed to parse SPIR-V resources: " << e.what() << "\n";
        return 1;
    }

    // 3. Open Output
    std::ofstream out(argv[2]);
    if (!out.is_open())
    {
        std::cerr << "Error: Failed to open output file: " << argv[2] << "\n";
        return 1;
    }

    out << "#pragma once\n";
    out << "#include <glm/glm.hpp>\n";
    out << "#include <cstdint>\n";
    out << "#include <cstddef>\n\n";
    out << "// AUTOMATICALLY GENERATED - DO NOT EDIT\n";
    out << "namespace ShaderGen {\n\n";

    std::set<uint32_t> emittedTypes;

    try
    {
        for (const auto& res : resources.push_constant_buffers)
        {
            generate_struct(comp, res.base_type_id, out, emittedTypes);
        }
        for (const auto& res : resources.uniform_buffers)
        {
            generate_struct(comp, res.base_type_id, out, emittedTypes);
        }
        for (const auto& res : resources.storage_buffers)
        {
            generate_struct(comp, res.base_type_id, out, emittedTypes);
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error during generation: " << e.what() << "\n";
        out.close();
        std::remove(argv[2]); // Delete partial file
        return 1;
    }

    out << "} // namespace ShaderGen\n";
    std::cout << "[ShaderStructGen] Success -> " << argv[2] << std::endl;

    return 0;
}
