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
// Std430 alignment helpers
// -----------------------------------------------------------------------------
static uint32_t baseAlignmentStd430(const spirv_cross::SPIRType& t)
{
    // Scalars
    if (t.columns == 1 && t.vecsize == 1)
    {
        return (t.width == 64) ? 8u : 4u;
    }

    // Vectors
    if (t.columns == 1)
    {
        if (t.vecsize == 2)
        {
            return (t.width == 64) ? 16u : 8u;
        }
        if (t.vecsize == 3 || t.vecsize == 4)
        {
            return (t.width == 64) ? 32u : 16u;
        }
    }

    // Matrices: treated as array of column vectors.
    if (t.columns > 1)
    {
        spirv_cross::SPIRType col = t;
        col.columns = 1;
        col.vecsize = t.vecsize;
        return baseAlignmentStd430(col);
    }

    return 16u;
}

static uint32_t typeAlignmentStd430(spirv_cross::Compiler& comp, const spirv_cross::SPIRType& type)
{
    if (!type.array.empty())
    {
        spirv_cross::SPIRType element = type;
        element.array.clear();
        return typeAlignmentStd430(comp, element);
    }

    if (type.basetype == spirv_cross::SPIRType::Struct)
    {
        uint32_t structAlign = 1u;
        for (const auto& memberTypeId : type.member_types)
        {
            const auto& memberType = comp.get_type(memberTypeId);
            structAlign = std::max(structAlign, typeAlignmentStd430(comp, memberType));
        }
        return structAlign;
    }

    return baseAlignmentStd430(type);
}

static size_t roundUp(size_t value, size_t alignment)
{
    if (alignment == 0u)
    {
        return value;
    }
    return (value + (alignment - 1u)) & ~(alignment - 1u);
}

// -----------------------------------------------------------------------------
// Helper: Get C++ type name
// -----------------------------------------------------------------------------
// Helper to turn SPIR-V enum into a readable string for error messages
std::string spirvTypeToString(const spirv_cross::SPIRType& type) {
    std::string base;
    switch (type.basetype) {
        case spirv_cross::SPIRType::Void:   base = "void"; break;
        case spirv_cross::SPIRType::Boolean:base = "bool"; break;
        case spirv_cross::SPIRType::SByte:  base = "int8"; break;
        case spirv_cross::SPIRType::UByte:  base = "uint8"; break;
        case spirv_cross::SPIRType::Short:  base = "int16"; break;
        case spirv_cross::SPIRType::UShort: base = "uint16"; break;
        case spirv_cross::SPIRType::Int:    base = "int32"; break;
        case spirv_cross::SPIRType::UInt:   base = "uint32"; break;
        case spirv_cross::SPIRType::Int64:  base = "int64"; break;
        case spirv_cross::SPIRType::UInt64: base = "uint64"; break;
        case spirv_cross::SPIRType::Half:   base = "float16"; break;
        case spirv_cross::SPIRType::Float:  base = "float32"; break;
        case spirv_cross::SPIRType::Double: base = "float64"; break;
        case spirv_cross::SPIRType::Struct: base = "struct"; break;
        case spirv_cross::SPIRType::Image:  base = "image"; break;
        case spirv_cross::SPIRType::SampledImage: base = "sampler"; break;
        case spirv_cross::SPIRType::Sampler:base = "samplerState"; break;
        default: base = "unknown"; break;
    }

    std::stringstream ss;
    ss << base << " (width: " << type.width << ", vec: " << type.vecsize << ", col: " << type.columns << ")";
    return ss.str();
}

std::string getCppTypeName(spirv_cross::Compiler& comp, const spirv_cross::SPIRType& type)
{
    // 1. Physical Storage Buffer Pointers / Buffer References
    if (type.pointer || type.storage == spv::StorageClassPhysicalStorageBuffer)
    {
        return "uint64_t";
    }

    // 2. 64-bit Scalars
    if (type.width == 64 && type.columns == 1 && type.vecsize == 1)
    {
        if (type.basetype == spirv_cross::SPIRType::UInt || type.basetype == spirv_cross::SPIRType::UInt64) return "uint64_t";
        if (type.basetype == spirv_cross::SPIRType::Int || type.basetype == spirv_cross::SPIRType::Int64) return "int64_t";
        if (type.basetype == spirv_cross::SPIRType::Float || type.basetype == spirv_cross::SPIRType::Double) return "double";
    }

    // 3. Structs
    if (type.basetype == spirv_cross::SPIRType::Struct)
    {
        std::string name = comp.get_name(type.self);
        if (name.empty()) name = comp.get_name(type.basetype);
        if (name.empty()) return "Struct_" + std::to_string(type.self);

        size_t dot = name.find_last_of('.');
        return (dot != std::string::npos) ? name.substr(dot + 1) : name;
    }

    // 4. Matrices (Floating Point)
    if (type.columns > 1 && type.basetype == spirv_cross::SPIRType::Float)
    {
        if (type.width == 32)
        {
            if (type.columns == 4 && type.vecsize == 4) return "glm::mat4";
            if (type.columns == 3 && type.vecsize == 3) return "glm::mat3";
            if (type.columns == 2 && type.vecsize == 2) return "glm::mat2";
        }
    }

    // 5. Vectors (32-bit)
    if (type.vecsize > 1 && type.columns == 1 && type.width == 32)
    {
        std::string prefix = "";
        if (type.basetype == spirv_cross::SPIRType::UInt) prefix = "u";
        else if (type.basetype == spirv_cross::SPIRType::Int) prefix = "i";
        else if (type.basetype == spirv_cross::SPIRType::Boolean) prefix = "b";
        else if (type.basetype != spirv_cross::SPIRType::Float) {
             // If it's a vector but not a common type, fall through to error
        }
        else {
            return "glm::vec" + std::to_string(type.vecsize);
        }
        return "glm::" + prefix + "vec" + std::to_string(type.vecsize);
    }

    // 6. 32-bit Scalars
    if (type.vecsize == 1 && type.columns == 1 && type.width == 32)
    {
        if (type.basetype == spirv_cross::SPIRType::Float) return "float";
        if (type.basetype == spirv_cross::SPIRType::UInt) return "uint32_t";
        if (type.basetype == spirv_cross::SPIRType::Int) return "int32_t";
        if (type.basetype == spirv_cross::SPIRType::Boolean) return "uint32_t /* bool */";
    }

    // 7. No mapping found: Throw detailed error
    std::string errorMsg = "Could not map SPIR-V type to C++: " + spirvTypeToString(type);
    throw std::runtime_error(errorMsg);
}
// -----------------------------------------------------------------------------
// Core Generator Function
// -----------------------------------------------------------------------------
void generateStruct(spirv_cross::Compiler& comp, uint32_t typeId, std::ofstream& out,
                    std::set<uint32_t>& emittedTypes)
{
    const auto& type = comp.get_type(typeId);

    // Skip types that are pointers (we represent these as uint64_t inline)
    if (type.pointer || type.storage == spv::StorageClassPhysicalStorageBuffer)
    {
        return;
    }

    if (emittedTypes.contains(typeId))
    {
        return;
    }
    emittedTypes.insert(typeId);

    // Recursively generate nested structs (only if they are NOT pointers)
    for (const auto& memberTypeId : type.member_types)
    {
        const auto& memberType = comp.get_type(memberTypeId);
        if (memberType.basetype == spirv_cross::SPIRType::Struct && !memberType.pointer)
        {
            generateStruct(comp, memberTypeId, out, emittedTypes);
        }
    }

    std::string structName = getCppTypeName(comp, type);
    size_t reflectedSize = comp.get_declared_struct_size(type);

    uint32_t structAlign = typeAlignmentStd430(comp, type);
    size_t totalSize = roundUp(reflectedSize, structAlign);
    out << "// SPIR-V ID: " << typeId << " | Size: " << totalSize << " bytes\n";
    out << "struct alignas(16) " << structName << " {\n";

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
        if (structName == "PushData" && cppType == "uint8_t")
        {
            throw std::runtime_error("Unmapped type in PushData. Refusing uint8_t fallback.");
        }

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

    for (const auto& m : members)
    {
        out << "static_assert(offsetof(" << structName << ", " << m.m_name << ") == "
            << m.m_offset << ", \"Offset mismatch for " << structName << "::" << m.m_name << "\");\n";
    }
    out << "\n";
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
