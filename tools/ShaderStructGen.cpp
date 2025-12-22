#include <spirv_cross.hpp>
#include <spirv_glsl.hpp>

#include <initializer_list>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <iomanip>

// =============================================================================
// UTILS
// =============================================================================

static std::vector<uint32_t> readSpirvWords(const std::string& path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("Failed to open SPIR-V file: " + path);

    const auto size = f.tellg();
    f.seekg(0, std::ios::beg);

    if (size <= 0 || (size % 4) != 0)
        throw std::runtime_error("SPIR-V size not a multiple of 4: " + path);

    std::vector<uint32_t> words(size / 4);
    f.read(reinterpret_cast<char*>(words.data()), size);
    if (!f) throw std::runtime_error("Failed to read SPIR-V file: " + path);

    return words;
}

static std::string sanitizeIdent(std::string s, const std::string& fallback)
{
    // Replace non-alphanumeric chars with _
    for (char& c : s)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            c = '_';
    }

    // Ensure it doesn't start with a digit
    if (!s.empty() && std::isdigit(static_cast<unsigned char>(s[0])))
        s = "_" + s;

    // Fallback if empty
    if (s.empty()) s = fallback;

    return s;
}

static bool isCppKeyword(const std::string& s)
{
    // Keep this set small-but-safe; extend if you ever hit an edge case.
    static const std::unordered_set<std::string> kw = {
        "alignas","alignof","and","and_eq","asm","auto","bitand","bitor","bool","break",
        "case","catch","char","char8_t","char16_t","char32_t","class","compl","concept",
        "const","consteval","constexpr","constinit","const_cast","continue","co_await",
        "co_return","co_yield","decltype","default","delete","do","double","dynamic_cast",
        "else","enum","explicit","export","extern","false","float","for","friend","goto",
        "if","inline","int","long","mutable","namespace","new","noexcept","not","not_eq",
        "nullptr","operator","or","or_eq","private","protected","public","register",
        "reinterpret_cast","requires","return","short","signed","sizeof","static",
        "static_assert","static_cast","struct","switch","template","this","thread_local",
        "throw","true","try","typedef","typeid","typename","union","unsigned","using",
        "virtual","void","volatile","wchar_t","while","xor","xor_eq"
    };
    return kw.count(s) != 0;
}

static std::string sanitizeNamespaceIdent(std::string s, const std::string& fallback)
{
    s = sanitizeIdent(std::move(s), fallback);
    // Avoid C++ keywords as namespace identifiers (legal in some compilers, but best avoided).
    if (isCppKeyword(s))
        s = "_" + s;
    return s;
}

static std::string stemFromPath(const std::string& p)
{
    std::string file = p;
    // Extract filename from path
    const auto slash = file.find_last_of("/\\");
    if (slash != std::string::npos) file = file.substr(slash + 1);

    // Strip strictly the ".spv" extension if present
    // We KEEP .vert, .frag, .comp to ensure uniqueness and match user conventions (e.g. cube_vert_PushConstants)
    if (file.size() > 4 && file.substr(file.size() - 4) == ".spv")
        file = file.substr(0, file.size() - 4);

    // Replace dots (e.g. cube.vert -> cube_vert)
    return sanitizeIdent(file, "Shader");
}

// =============================================================================
// INSPECTOR
// =============================================================================

// Subclass to access protected IR to find ALL structs, not just active resources.
class StructInspector : public spirv_cross::Compiler
{
public:
    using Compiler::Compiler;

    std::vector<uint32_t> getAllStructIds() const
    {
        std::vector<uint32_t> ids;
        const size_t bound = ir.ids.size();
        for (uint32_t i = 0; i < bound; ++i)
        {
            if (ir.ids[i].get_type() == spirv_cross::TypeType)
            {
                const auto& t = get_type(i);
                if (t.basetype == spirv_cross::SPIRType::Struct)
                    ids.push_back(i);
            }
        }
        return ids;
    }
};

// =============================================================================
// GENERATOR
// =============================================================================

class StructGenerator
{
public:
    StructGenerator(StructInspector& compiler, std::ostream& out, const std::string& shaderName)
        : comp(compiler), os(out), shaderStem(shaderName)
        , shaderNamespace(sanitizeNamespaceIdent(shaderName, "Shader"))
    {
    }

    void run()
    {
        emitHeader();

        // 1. Process Explicit Resources (Contextual Naming)
        const auto resources = comp.get_shader_resources();

        processResourceList(resources.push_constant_buffers, "PushConstants");
        processResourceList(resources.uniform_buffers, "UBO");
        processResourceList(resources.storage_buffers, "SSBO");
        processResourceList(resources.stage_outputs, "Output");

        // 2. Process All Remaining Internal Structs
        std::vector<uint32_t> allStructs = comp.getAllStructIds();
        std::sort(allStructs.begin(), allStructs.end());

        for (uint32_t id : allStructs)
        {
            if (isEmitted(id)) continue;

            // Filter out internal built-ins (gl_PerVertex, etc.)
            if (isBuiltIn(id)) continue;

            emitDependencies(id);

            // For internal structs, get_name might be empty.
            // allocateName ensures we get "AnonStruct_ID" if genericHint is used.
            std::string name = comp.get_name(id);
            std::string finalName = allocateName(id, name, "AnonStruct");

            idToName[id] = finalName;
            emitStruct(id, finalName, false);
        }

        emitFooter();
    }

private:
    StructInspector& comp;
    std::ostream& os;
    std::string shaderStem;
    std::string shaderNamespace;

    std::unordered_set<uint32_t> emittedIds;
    std::unordered_map<std::string, uint32_t> nameMap;
    std::unordered_map<uint32_t, std::string> idToName;

    struct CppType
    {
        std::string name;
        size_t sizeBytes;
        bool hasRuntimeArray;
        bool isBlob;
    };

    bool isEmitted(uint32_t id) const { return emittedIds.count(id) != 0; }

    bool isBuiltIn(uint32_t id) const
    {
        if (comp.has_decoration(id, spv::DecorationBlock))
        {
            const auto& type = comp.get_type(id);
            for (uint32_t i = 0; i < type.member_types.size(); ++i)
            {
                if (comp.has_member_decoration(id, i, spv::DecorationBuiltIn))
                    return true;
            }
        }
        return false;
    }

    // Allocate Unique Name
    // Priorities:
    // 1. Existing spirv name (if not generic)
    // 2. candidateName (from resource instance)
    // 3. genericHint (fallback)
    // If result is Generic ("Block"), prefix with ShaderStem.
    std::string allocateName(uint32_t typeId, const std::string& candidateName, const std::string& genericHint)
    {
        // 1. Try Type Name (Debug Info)
        std::string base = comp.get_name(typeId);

        // 2. Try Candidate (Instance Name)
        if (base.empty()) base = candidateName;

        // 3. Fallback
        if (base.empty()) base = genericHint;

        // FIX: If SPIR-V says this is 8 bytes, but our type map thought it was 4 (common for BDA/pointers)
        // force it to DeviceAddress to maintain alignment.


        base = sanitizeIdent(base, "AnonStruct");

        // 4. Handle Generic Names
        static const std::unordered_set<std::string> genericNames = {
            "Block", "PushConstants", "Uniforms", "Constants", "Globals", "UBO", "SSBO", "Params", "pc",
            "Vertex", "Fragment", "Compute", "Input", "Output", "Material", "Data"
        };

        bool isGeneric = genericNames.count(base) || base.find("Block") != std::string::npos;

        // If generic, prefix with shader stem (e.g. "cube_vert_PushConstants")
        if (isGeneric)
        {
            base = shaderStem + "_" + base;
        }

        // 5. Deduplicate
        if (nameMap.find(base) == nameMap.end())
        {
            nameMap[base] = typeId;
            return base;
        }

        // Try appending ID
        std::string withId = base + "_" + std::to_string(typeId);
        if (nameMap.find(withId) == nameMap.end())
        {
            nameMap[withId] = typeId;
            return withId;
        }

        // Try appending counter
        int counter = 1;
        while (true)
        {
            std::string tryName = withId + "_" + std::to_string(counter++);
            if (nameMap.find(tryName) == nameMap.end())
            {
                nameMap[tryName] = typeId;
                return tryName;
            }
        }
    }

    void emitHeader()
    {
        os << "#pragma once\n";
        os << "#include <array>\n";
        os << "#include <cstddef>\n";
        os << "#include <cstdint>\n";
        os << "#include <type_traits>\n";
        os << "#include \"pnkr/renderer/shadergen_common.hpp\"\n\n";
        os << "namespace ShaderGen {\n";
        os << "namespace " << shaderNamespace << " {\n\n";
    }

    void emitFooter()
    {
        os << "} // namespace " << shaderNamespace << "\n";
        os << "} // namespace ShaderGen\n";
    }

    void processResourceList(const spirv_cross::SmallVector<spirv_cross::Resource>& resources, const std::string& hint)
    {
        for (const auto& res : resources)
        {
            uint32_t typeId = res.base_type_id;
            if (isEmitted(typeId)) continue;

            emitDependencies(typeId);

            // Allocate name using the Type ID, but suggesting the Resource Name as fallback
            std::string finalName = allocateName(typeId, res.name, hint);
            idToName[typeId] = finalName;

            emitStruct(typeId, finalName, true);
        }
    }

    void emitDependencies(uint32_t typeId)
    {
        const auto& type = comp.get_type(typeId);
        for (const auto& memberTypeId : type.member_types)
        {
            const auto& memberType = comp.get_type(memberTypeId);
            if (memberType.basetype == spirv_cross::SPIRType::Struct)
            {
                if (!isEmitted(memberType.self) && !isBuiltIn(memberType.self))
                {
                    emitDependencies(memberType.self);

                    std::string nestedName = allocateName(memberType.self, comp.get_name(memberType.self), "Struct");
                    idToName[memberType.self] = nestedName;

                    emitStruct(memberType.self, nestedName, false);
                }
            }
        }
    }

    CppType mapType(uint32_t typeId, uint32_t memberIdx, uint32_t structId)
    {
        auto type = comp.get_type(typeId);

        // Arrays
        if (!type.array.empty())
        {
            bool checkStride = comp.has_member_decoration(structId, memberIdx, spv::DecorationOffset);
            uint32_t stride = 0;
            if (checkStride) stride = comp.type_struct_member_array_stride(comp.get_type(structId), memberIdx);

            auto tempType = type;
            tempType.array.clear();
            tempType.array_size_literal.clear();

            CppType elType = mapBaseType(tempType);

            // Stride mismatch check (e.g. std140 float array has 16-byte stride vs C++ 4-byte)
            if (checkStride && stride > 0 && stride != elType.sizeBytes)
                return CppType{"/*stride_mismatch*/", 0, false, true};

            if (type.array.back() == 0)
                return {"ShaderGen::RuntimeArray<" + elType.name + ">", 0, true, false};
            else
            {
                size_t totalSize = elType.sizeBytes * type.array.back();
                return {
                    "std::array<" + elType.name + ", " + std::to_string(type.array.back()) + ">", totalSize, false,
                    false
                };
            }
        }

        return mapBaseType(type);
    }

    CppType mapBaseType(const spirv_cross::SPIRType& t)
    {
        // Device Address / Buffer Reference
        if (t.storage == spv::StorageClassPhysicalStorageBuffer || t.pointer)
            return {"ShaderGen::DeviceAddress", 8, false, false};

        // Structs
        if (t.basetype == spirv_cross::SPIRType::Struct)
        {
            if (idToName.count(t.self))
                return {idToName[t.self], (size_t)comp.get_declared_struct_size(t), false, false};

            return {"/*unknown_struct*/", 0, false, true};
        }

        // Matrices
        if (t.columns > 1)
        {
            if (t.basetype == spirv_cross::SPIRType::Float && t.width == 32 && t.vecsize == 4 && t.columns == 4)
                return {"ShaderGen::Mat4", 64, false, false};
            return {"/*complex_mat*/", (size_t)(t.width / 8 * t.vecsize * t.columns), false, true};
        }

        // Vectors
        if (t.vecsize > 1)
        {
            size_t size = (t.width / 8) * t.vecsize;
            if (t.basetype == spirv_cross::SPIRType::Float && t.width == 32)
            {
                if (t.vecsize == 4) return {"ShaderGen::Float4", 16, false, false};
                if (t.vecsize == 3) return {"ShaderGen::Float3", 8, false, false};
                if (t.vecsize == 2) return {"ShaderGen::Float2", 8, false, false};
            }
            std::string scalar = mapScalarName(t.basetype, t.width);
            return {"std::array<" + scalar + ", " + std::to_string(t.vecsize) + ">", size, false, false};
        }

        // Scalars
        size_t size = t.width / 8;
        return {mapScalarName(t.basetype, t.width), size, false, false};
    }

    std::string mapScalarName(spirv_cross::SPIRType::BaseType bt, uint32_t width)
    {
        switch (bt)
        {
        case spirv_cross::SPIRType::Float: return width == 64 ? "double" : "float";
        case spirv_cross::SPIRType::Int:
            if (width == 64) return "int64_t";
            if (width == 16) return "int16_t";
            if (width == 8) return "int8_t";
            return "int32_t";
        case spirv_cross::SPIRType::UInt:
            if (width == 64) return "uint64_t";
            if (width == 16) return "uint16_t";
            if (width == 8) return "uint8_t";
            return "uint32_t";
        case spirv_cross::SPIRType::Boolean: return "uint32_t";
        default: return "uint32_t";
        }
    }

    void emitStruct(uint32_t typeId, const std::string& structName, bool isBlock)
    {
        if (isEmitted(typeId)) return;

        const auto& type = comp.get_type(typeId);
        bool hasOffset = !type.member_types.empty() && comp.has_member_decoration(typeId, 0, spv::DecorationOffset);

        struct MemberInfo
        {
            std::string name;
            std::string typeName;
            uint32_t offset;
            uint32_t size;
            bool isRuntime;
        };
        std::vector<MemberInfo> members;

        uint32_t cursor = 0;
        bool hasRuntimeArray = false;

        for (uint32_t i = 0; i < type.member_types.size(); ++i)
        {
            std::string mName = comp.get_member_name(typeId, i);
            if (mName.empty()) mName = "m" + std::to_string(i);

            if (mName.find("gl_") == 0) mName = sanitizeIdent(mName, "gl_var");
            else mName = sanitizeIdent(mName, "var");

            CppType ct = mapType(type.member_types[i], i, typeId);

            uint32_t mOffset = cursor;
            uint32_t mSize = (uint32_t)ct.sizeBytes;

            if (hasOffset) {
                mOffset = comp.type_struct_member_offset(type, i);
                // FETCH REAL SIZE FROM SPIR-V FIRST
                mSize = (uint32_t)comp.get_declared_struct_member_size(type, i);
            }

            // 2. Detect if it's a pointer (DeviceAddress)
            if (hasOffset && mSize == 8 && (ct.sizeBytes == 4 || comp.get_type(type.member_types[i]).basetype == spirv_cross::SPIRType::Struct))
            {
                ct.name = "ShaderGen::DeviceAddress";
                ct.sizeBytes = 8;
            }

            if (hasOffset && mSize == 8 && ct.sizeBytes == 8)
            {
                ct.name = "ShaderGen::DeviceAddress";
                ct.sizeBytes = 8;
            }

            if (hasOffset && mSize == 8 && ct.sizeBytes == 4) {
                ct.name = "ShaderGen::DeviceAddress";
                ct.sizeBytes = 8;
            }
            if (ct.isBlob || (hasOffset && mSize != ct.sizeBytes && !ct.hasRuntimeArray)) {
                ct.name = "std::array<std::byte, " + std::to_string(mSize) + ">";
                ct.sizeBytes = mSize;
            }

            if (hasOffset)
            {
                mOffset = comp.type_struct_member_offset(type, i);
                mSize = (uint32_t)comp.get_declared_struct_member_size(type, i);
            }

            // Fallback to byte blob if sizing/layout is complex
            if (ct.isBlob || (hasOffset && mSize != ct.sizeBytes && !ct.hasRuntimeArray))
            {
                ct.name = "std::array<std::byte, " + std::to_string(mSize) + ">";
                ct.sizeBytes = mSize;
            }

            members.push_back({mName, ct.name, mOffset, mSize, ct.hasRuntimeArray});
            cursor = mOffset + mSize;
            if (ct.hasRuntimeArray) hasRuntimeArray = true;
        }


        if (hasOffset)
        {
            std::sort(members.begin(), members.end(), [](auto& a, auto& b) { return a.offset < b.offset; });
        }

        // Emit Struct Definition
        os << "#pragma pack(push, 1)\n";
        os << "struct " << structName << "\n{\n";

        uint32_t currentPos = 0;
        int padCount = 0;

        for (const auto& m : members)
        {
            if (hasOffset && m.offset > currentPos)
            {
                os << "    ShaderGen::Pad<" << (m.offset - currentPos) << "> _pad" << padCount++ << ";\n";
                currentPos = m.offset;
            }
            os << "    " << m.typeName << " " << m.name << ";\n";
            currentPos += m.size;
        }

        if (hasOffset && !hasRuntimeArray)
        {
            uint32_t declaredSize = (uint32_t)comp.get_declared_struct_size(type);
            if (currentPos < declaredSize)
                os << "    ShaderGen::Pad<" << (declaredSize - currentPos) << "> _pad" << padCount++ << ";\n";
        }

        os << "};\n#pragma pack(pop)\n";

        // Static Asserts
        os << "static_assert(std::is_standard_layout_v<" << structName << ">);\n";
        if (hasOffset && !hasRuntimeArray)
        {
            os << "static_assert(sizeof(" << structName << ") == " << comp.get_declared_struct_size(type) << ");\n";
            for (const auto& m : members)
                os << "static_assert(offsetof(" << structName << ", " << m.name << ") == " << m.offset << ");\n";
        }
        os << "\n";

        emittedIds.insert(typeId);
    }
};

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "Usage: ShaderStructGen <input.spv> <output.h>\n";
        return 1;
    }

    try
    {
        auto spirv = readSpirvWords(argv[1]);
        StructInspector comp(spirv);

        std::ofstream os(argv[2], std::ios::binary);
        if (!os) throw std::runtime_error("Output file open failed");

        // Pass stem including the shader stage for better context (cube_vert)
        StructGenerator gen(comp, os, stemFromPath(argv[1]));
        gen.run();

        std::cout << "Generated: " << argv[2] << "\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
