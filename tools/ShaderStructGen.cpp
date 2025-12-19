// tools/ShaderStructGen.cpp
#include <algorithm>
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
std::string getCppTypeName(spirv_cross::Compiler& comp, const spirv_cross::SPIRType& type)
{
    if (type.storage == spv::StorageClassPhysicalStorageBuffer)
    {
        return "uint64_t";
    }

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

    if (type.width == 64)
    {
        if (type.basetype == spirv_cross::SPIRType::UInt) return "uint64_t";
        if (type.basetype == spirv_cross::SPIRType::Int) return "int64_t";
        if (type.basetype == spirv_cross::SPIRType::Float) return "double";
    }

    if (type.basetype == spirv_cross::SPIRType::Float)
    {
        if (type.columns == 4 && type.vecsize == 4)
        {
            return "glm::mat4";
        }
        if (type.columns == 3 && type.vecsize == 3)
        {
            return "glm::mat3";
        }
        if (type.vecsize == 4)
        {
            return "glm::vec4";
        }
        if (type.vecsize == 3)
        {
            return "glm::vec3";
        }
        if (type.vecsize == 2)
        {
            return "glm::vec2";
        }
        return "float";
    }

    if (type.basetype == spirv_cross::SPIRType::Int)
    {
        if (type.vecsize == 4)
        {
            return "glm::ivec4";
        }
        if (type.vecsize == 3)
        {
            return "glm::ivec3";
        }
        if (type.vecsize == 2)
        {
            return "glm::ivec2";
        }
        return "int32_t";
    }

    if (type.basetype == spirv_cross::SPIRType::UInt)
    {
        if (type.vecsize == 4)
        {
            return "glm::uvec4";
        }
        if (type.vecsize == 3)
        {
            return "glm::uvec3";
        }
        if (type.vecsize == 2)
        {
            return "glm::uvec2";
        }
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
void generateStruct(spirv_cross::Compiler& comp, uint32_t typeId, std::ofstream& out,
                    std::set<uint32_t>& emittedTypes)
{
    if (emittedTypes.contains(typeId) != 0u)
    {
        return;
    }
    emittedTypes.insert(typeId);

    const auto& type = comp.get_type(typeId);

    if (type.storage == spv::StorageClassPhysicalStorageBuffer)
    {
        return;
    }

    // Recursively generate nested structs
    for (const auto& memberTypeId : type.member_types)
    {
        const auto& memberType = comp.get_type(memberTypeId);
        if (memberType.basetype == spirv_cross::SPIRType::Struct)
        {
            generateStruct(comp, memberTypeId, out, emittedTypes);
        }
    }

    std::string structName = getCppTypeName(comp, type);
    size_t reflectedSize = comp.get_declared_struct_size(type);

    // Round the size up to the struct alignment (C++ tail padding behavior)
    // VULKAN FIX: Round up to nearest 16 bytes to match C++ alignment/padding
    size_t totalSize = (reflectedSize + 15) & ~15;
    out << "// SPIR-V ID: " << typeId << " | Size: " << totalSize << " bytes\n";
    out << "struct " << structName << " {\n";

    struct Member
    {
        uint32_t m_index;
        uint32_t m_offset;
        uint32_t m_size;
        uint32_t m_matrixStride;
        uint32_t m_arrayStride;
        std::string m_name;
        spirv_cross::SPIRType m_type;
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

        Member member = {
            .m_index = i,
            .m_offset = comp.type_struct_member_offset(type, i),
            .m_size = (uint32_t)comp.get_declared_struct_member_size(type, i),
            .m_matrixStride = matStride,
            .m_arrayStride = arrStride,
            .m_name = comp.get_member_name(type.self, i),
            .m_type = memberType
        };

        if (member.m_name.empty())
        {
            member.m_name = "member_" + std::to_string(i);
        }
        members.push_back(member);
    }

    // Sort by memory offset
    std::ranges::sort(members, [](const Member& a, const Member& b)
    {
        return a.m_offset < b.m_offset;
    });

    uint32_t currentOffset = 0;

    for (const auto& m : members)
    {
        // Padding
        if (m.m_offset > currentOffset)
        {
            out << "    uint8_t _pad_" << currentOffset << "[" << (m.m_offset - currentOffset) << "];\n";
        }

        std::string cppType = getCppTypeName(comp, m.m_type);

        // Output Field
        if (!m.m_type.array.empty())
        {
            uint32_t arraySize = m.m_type.array[0];
            if (m.m_arrayStride > 0 && arraySize > 1)
            {
                out << "    // Array Stride: " << m.m_arrayStride << "\n";
            }
            out << "    " << cppType << " " << m.m_name << "[" << arraySize << "];\n";
        }
        else
        {
            out << "    " << cppType << " " << m.m_name << ";\n";
        }

        currentOffset = m.m_offset + m.m_size;
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

    std::cout << "[ShaderStructGen] Processing: " << argv[1] << '\n';

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
    std::vector<uint32_t> spirvBinary(fileSize / 4);
    if (!file.read((char*)spirvBinary.data(), fileSize))
    {
        std::cerr << "Error: Failed to read file content.\n";
        return 1;
    }

    // 2. Initialize Compiler
    spirv_cross::Compiler comp(spirvBinary);
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
            generateStruct(comp, res.base_type_id, out, emittedTypes);
        }
        for (const auto& res : resources.uniform_buffers)
        {
            generateStruct(comp, res.base_type_id, out, emittedTypes);
        }
        for (const auto& res : resources.storage_buffers)
        {
            generateStruct(comp, res.base_type_id, out, emittedTypes);
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
    std::cout << "[ShaderStructGen] Success -> " << argv[2] << '\n';

    return 0;
}
