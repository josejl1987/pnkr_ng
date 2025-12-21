#include <spirv_cross.hpp>
#include <spirv_glsl.hpp>

#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

static std::vector<uint32_t> readSpirvWords(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("Failed to open SPIR-V file: " + path);

    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    f.seekg(0, std::ios::beg);

    if (size <= 0 || (size % 4) != 0)
        throw std::runtime_error("SPIR-V size not a multiple of 4: " + path);

    std::vector<uint32_t> words(static_cast<size_t>(size / 4));
    f.read(reinterpret_cast<char*>(words.data()), size);
    if (!f)
        throw std::runtime_error("Failed to read SPIR-V file: " + path);

    return words;
}

static std::string toUpperIdent(std::string s)
{
    for (char& c : s)
    {
        if (std::isalnum(static_cast<unsigned char>(c))) c = static_cast<char>(std::toupper(c));
        else c = '_';
    }
    return s;
}

static std::string sanitizeIdent(std::string s, const std::string& fallback)
{
    for (char& c : s)
    {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
            c = '_';
    }
    if (s.empty()) s = fallback;
    if (std::isdigit(static_cast<unsigned char>(s[0])))
        s = "_" + s;
    return s;
}

struct CppType
{
    std::string name;
    size_t sizeBytes = 0;
    bool hasRuntimeArray = false; // for structs
};

// Forward decl
static CppType mapType(spirv_cross::Compiler& comp, const spirv_cross::SPIRType& t);

static size_t scalarSizeBytes(const spirv_cross::SPIRType& t)
{
    return static_cast<size_t>(t.width / 8);
}

static bool isPhysicalPointerLike(const spirv_cross::SPIRType& t)
{
    // SPIRV-Cross represents buffer_reference / physical storage buffer pointers using
    // StorageClassPhysicalStorageBuffer in many toolchains.
    return t.storage == spv::StorageClassPhysicalStorageBuffer;
}

static CppType mapScalar(const spirv_cross::SPIRType& t)
{
    using Base = spirv_cross::SPIRType::BaseType;

    switch (t.basetype)
    {
    case Base::Boolean: return {"uint32_t", 4, false};

    case Base::SByte: return {"int8_t", 1, false};
    case Base::UByte: return {"uint8_t", 1, false};
    case Base::Short: return {"int16_t", 2, false};
    case Base::UShort: return {"uint16_t", 2, false};
    case Base::Int: return {"int32_t", 4, false};
    case Base::UInt: return {"uint32_t", 4, false};
    case Base::Int64: return {"int64_t", 8, false};
    case Base::UInt64: return {"uint64_t", 8, false};

    case Base::Float: return {"float", 4, false};
    case Base::Double: return {"double", 8, false};

    case Base::Half:
        // Store IEEE-754 binary16 payload (raw bits), do not guess alignment/packing.
        // Layout correctness is ensured by SPIR-V offsets + Pad<N>.
        return {"uint16_t", 2, false};

    case Base::BFloat16:
        // Also raw bits (bf16). Same rationale.
        return {"uint16_t", 2, false};

    default:
        break;
    }

    throw std::runtime_error(
        "Unsupported scalar SPIR-V BaseType: " +
        std::to_string(static_cast<int>(t.basetype)));
}


static CppType mapVector(spirv_cross::Compiler& /*comp*/, const spirv_cross::SPIRType& t)
{
    // Only handle scalar vectors (no matrices here).
    spirv_cross::SPIRType scalar = t;
    scalar.vecsize = 1;
    scalar.columns = 1;

    const CppType sc = mapScalar(scalar);

    // Fail-fast: only allow sane scalar element sizes.
    if (sc.sizeBytes != 1 && sc.sizeBytes != 2 && sc.sizeBytes != 4 && sc.sizeBytes != 8)
    {
        std::ostringstream oss;
        oss << "Unsupported vector scalar size: " << sc.sizeBytes
            << " (basetype=" << static_cast<int>(scalar.basetype)
            << ", width=" << scalar.width << ")";
        throw std::runtime_error(oss.str());
    }

    // Use tight POD wrappers for common float vectors to preserve GLM ergonomics
    // without forcing extra alignment.
    if (sc.name == "float")
    {
        if (t.vecsize == 2) return {"ShaderGen::Float2", 8, false};
        if (t.vecsize == 4) return {"ShaderGen::Float4", 16, false};
    }

    // Generic tight storage; rely on SPIR-V offsets + Pad<> for layout correctness.
    std::ostringstream oss;
    oss << "std::array<" << sc.name << ", " << t.vecsize << ">";
    return {oss.str(), sc.sizeBytes * static_cast<size_t>(t.vecsize), false};
}

static CppType mapMatrix(spirv_cross::Compiler& /*comp*/, const spirv_cross::SPIRType& t)
{
    // We only support float32 mat4 explicitly, because anything else needs stride rules
    // (std140/std430, matrix stride, row/column major) and we are not guessing here.

    // mat4: 4 columns of vec4 float32
    if (t.basetype == spirv_cross::SPIRType::Float && t.width == 32 && t.vecsize == 4 && t.columns == 4)
    {
        // Tight POD wrapper with GLM assignment support (no forced alignment).
        return {"ShaderGen::Mat4", 64, false};
    }

    std::ostringstream oss;
    oss << "Unsupported matrix type: basetype=" << static_cast<int>(t.basetype)
        << " width=" << t.width
        << " vecsize=" << t.vecsize
        << " columns=" << t.columns;
    throw std::runtime_error(oss.str());
}

static CppType mapArray(spirv_cross::Compiler& comp, const spirv_cross::SPIRType& t)
{
    spirv_cross::SPIRType elem = t;
    elem.array.clear();
    elem.array_size_literal.clear();

    const CppType e = mapType(comp, elem);

    // Runtime array (unsized) OR non-literal array size.
    if (!t.array.empty())
    {
        const bool lastIsRuntime =
            (t.array.back() == 0 && !t.array_size_literal.empty() && !t.array_size_literal.back());

        const bool hasNonLiteral =
            (!t.array_size_literal.empty() &&
             std::any_of(t.array_size_literal.begin(), t.array_size_literal.end(),
                         [](bool lit) { return !lit; }));

        if (lastIsRuntime || hasNonLiteral)
        {
            std::ostringstream oss;
            oss << "ShaderGen::RuntimeArray<" << e.name << ">";
            return {oss.str(), 0, true};
        }
    }

    // Static array
    size_t count = 1;
    for (size_t i = 0; i < t.array.size(); ++i)
    {
        if (!t.array_size_literal[i])
        {
            throw std::runtime_error(
                "Non-literal array size in interface block is not supported here (expected RuntimeArray path).");
        }
        count *= static_cast<size_t>(t.array[i]);
    }

    std::ostringstream oss;
    oss << "std::array<" << e.name << ", " << count << ">";
    return {oss.str(), e.sizeBytes * count, e.hasRuntimeArray};
}

static std::unordered_map<uint32_t, std::string> g_typeNameOverride;
std::unordered_set<uint32_t> emittedStructTypes;

static CppType mapStruct(spirv_cross::Compiler& comp, const spirv_cross::SPIRType& t)
{
    // If we emitted this struct under a different C++ name, use that.
    if (auto it = g_typeNameOverride.find(t.self); it != g_typeNameOverride.end())
    {
        const size_t declaredSize = static_cast<size_t>(comp.get_declared_struct_size(t));
        bool hasRuntime = false;

        for (uint32_t mi = 0; mi < t.member_types.size(); ++mi)
        {
            const auto& mt = comp.get_type(t.member_types[mi]);
            if (!mt.array.empty())
            {
                if ((!mt.array_size_literal.empty() && !mt.array_size_literal.back()) ||
                    (!mt.array.empty() && mt.array.back() == 0))
                    hasRuntime = true;
            }
        }

        return {it->second, declaredSize, hasRuntime};
    }

    // Default: use SPIR-V name.
    std::string n = comp.get_name(t.self);
    n = sanitizeIdent(n, "AnonStruct");

    const size_t declaredSize = static_cast<size_t>(comp.get_declared_struct_size(t));

    bool hasRuntime = false;
    for (uint32_t mi = 0; mi < t.member_types.size(); ++mi)
    {
        const auto& mt = comp.get_type(t.member_types[mi]);
        if (!mt.array.empty())
        {
            if ((!mt.array_size_literal.empty() && !mt.array_size_literal.back()) ||
                (!mt.array.empty() && mt.array.back() == 0))
                hasRuntime = true;
        }
    }

    return {n, declaredSize, hasRuntime};
}


static CppType mapType(spirv_cross::Compiler& comp, const spirv_cross::SPIRType& t)
{
    // Physical storage pointer / buffer_reference
    if (isPhysicalPointerLike(t))
        return {"ShaderGen::DeviceAddress", 8, false};

    if (!t.array.empty())
        return mapArray(comp, t);

    if (t.columns > 1)
        return mapMatrix(comp, t);

    if (t.vecsize > 1)
        return mapVector(comp, t);

    if (t.basetype == spirv_cross::SPIRType::Struct)
        return mapStruct(comp, t);

    return mapScalar(t);
}

struct MemberEmit
{
    std::string name;
    std::string cppType;
    uint32_t spirvOffset = 0;
    uint32_t spirvSize = 0;
    size_t cppSize = 0;
    bool runtimeArray = false;
};

static std::string deriveStructName(const std::string& shaderStem, const std::string& blockName)
{
    // If blockName is empty/generic, prefix with shader stem to avoid collisions.
    std::string b = sanitizeIdent(blockName, "Block");
    std::string s = sanitizeIdent(shaderStem, "Shader");

    // Heuristic: if block is something very generic, prefix it.
    const std::unordered_set<std::string> generic = {
        "PushConstants", "constants", "Constants", "Data"
    };

    if (generic.count(b) != 0)
        return s + "_" + b;

    // If it already looks shader-specific, keep it.
    return b;
}

static void emitStructRecursive(
    spirv_cross::Compiler& comp,
    uint32_t typeId,
    std::unordered_set<uint32_t>& emittedStructTypes,
    const std::function<void(uint32_t)>& emitStructFn)
{
    if (emittedStructTypes.contains(typeId))
        return;

    const auto& t = comp.get_type(typeId);
    if (t.basetype != spirv_cross::SPIRType::Struct)
        throw std::runtime_error("emitStructRecursive(): non-struct");

    for (uint32_t i = 0; i < (uint32_t)t.member_types.size(); ++i)
    {
        const auto& mt = comp.get_type(t.member_types[i]);
        if (mt.basetype == spirv_cross::SPIRType::Struct)
            emitStructRecursive(comp, mt.self, emittedStructTypes, emitStructFn);
    }

    emitStructFn(typeId);
    emittedStructTypes.insert(typeId);
}


static void emitStruct(std::ostream& os,
                       spirv_cross::Compiler& comp,
                       const std::string& shaderStem,
                       const spirv_cross::Resource& res,
                       const spirv_cross::SPIRType& st)
{
    std::string blockName = comp.get_name(res.base_type_id);

    // Fallbacks: sometimes SPIR-V might not preserve type names (rare, but can happen)
    if (blockName.empty())
        blockName = comp.get_name(res.id); // instance id name (often "perFrame")
    if (blockName.empty())
        blockName = res.name; // last resort

    blockName = sanitizeIdent(blockName, "Block");

    const std::string structName = deriveStructName(shaderStem, blockName);


    // Collect members
    std::vector<MemberEmit> members;
    members.reserve(st.member_types.size());

    bool hasRuntimeArray = false;

    for (uint32_t i = 0; i < st.member_types.size(); ++i)
    {
        const auto& mt = comp.get_type(st.member_types[i]);

        std::string mname = comp.get_member_name(st.self, i);
        mname = sanitizeIdent(mname, "m" + std::to_string(i));

        const uint32_t offset = comp.type_struct_member_offset(st, i);
        const uint32_t msize = comp.get_declared_struct_member_size(st, i);

        CppType ct = mapType(comp, mt);

        // Special-case “float mat4” etc already mapped; otherwise, if we emitted the placeholder for unsupported matrices,
        // fall back to byte array sized to SPIR-V member size.
        std::string cppType = ct.name;
        size_t cppSize = ct.sizeBytes;

        if (cppType.find("/*unsupported_matrix*/") != std::string::npos)
        {
            std::ostringstream tmp;
            tmp << "std::array<std::byte, " << msize << ">";
            cppType = tmp.str();
            cppSize = msize;
        }

        // If the member is a struct, refer by its name (it will be emitted elsewhere if it is an interface block),
        // but we still must ensure size correctness. If our computed size differs, fall back to bytes.
        if (mt.basetype == spirv_cross::SPIRType::Struct && cppSize != msize)
        {
            // Use raw bytes to preserve layout.
            std::ostringstream tmp;
            tmp << "std::array<std::byte, " << msize << ">";
            cppType = tmp.str();
            cppSize = msize;
        }

        const bool runtimeArr = ct.hasRuntimeArray;
        hasRuntimeArray = hasRuntimeArray || runtimeArr;

        const bool looksRuntime = runtimeArr || (msize == 0 && !mt.array.empty());
        if (looksRuntime && (i + 1) != st.member_types.size())
        {
            std::ostringstream err;
            err << "Runtime array member '" << mname << "' in struct '" << structName
                << "' is not last member. Refusing to generate invalid C++ layout.";
            throw std::runtime_error(err.str());
        }

        members.push_back(MemberEmit{
            .name = mname,
            .cppType = cppType,
            .spirvOffset = offset,
            .spirvSize = msize,
            .cppSize = cppSize,
            .runtimeArray = runtimeArr
        });
    }

    // Sort by offset (should already be, but keep robust)
    std::sort(members.begin(), members.end(), [](const MemberEmit& a, const MemberEmit& b)
    {
        return a.spirvOffset < b.spirvOffset;
    });

    const uint32_t structSize = comp.get_declared_struct_size(st);

    os << "\n#pragma pack(push, 1)\n";
    os << "struct " << structName << "\n{\n";

    uint32_t cursor = 0;
    uint32_t padIdx = 0;

    for (size_t i = 0; i < members.size(); ++i)
    {
        const auto& m = members[i];

        if (cursor < m.spirvOffset)
        {
            const uint32_t pad = m.spirvOffset - cursor;
            os << "    ShaderGen::Pad<" << pad << "> _pad" << padIdx++ << ";\n";
            cursor += pad;
        }

        os << "    " << m.cppType << " " << m.name << ";\n";
        cursor += static_cast<uint32_t>(m.cppSize);
    }

    // Tail padding to declared size (only if no runtime array at end).
    if (!hasRuntimeArray && cursor < structSize)
    {
        const uint32_t pad = structSize - cursor;
        os << "    ShaderGen::Pad<" << pad << "> _pad" << padIdx++ << ";\n";
        cursor += pad;
    }

    os << "};\n";
    os << "#pragma pack(pop)\n";

    // Static asserts (do not emit sizeof asserts if runtime array exists)
    os << "static_assert(std::is_standard_layout_v<" << structName << ">, \"Must be standard-layout\");\n";

    if (!hasRuntimeArray && structSize != 0)
        os << "static_assert(sizeof(" << structName << ") == " << structSize << ", \"Size mismatch for " << structName
            << "\");\n";

    for (const auto& m : members)
    {
        os << "static_assert(offsetof(" << structName << ", " << m.name << ") == " << m.spirvOffset
            << ", \"Offset mismatch for " << structName << "::" << m.name << "\");\n";
    }
}

static void emitStructByTypeId(std::ostream& os,
                               spirv_cross::Compiler& comp,
                               uint32_t typeId)
{
    const auto& st = comp.get_type(typeId);
    if (st.basetype != spirv_cross::SPIRType::Struct)
        throw std::runtime_error("emitStructByTypeId(): typeId is not a struct");

    std::string structName = sanitizeIdent(comp.get_name(typeId), "AnonStruct");

    // Register mapping for references.
    g_typeNameOverride[st.self] = structName;

    // Emit as if it were a block with that exact name, without any shaderStem heuristic.
    spirv_cross::Resource fake{};
    fake.base_type_id = typeId;

    const std::string emptyStem;
    emitStruct(os, comp, emptyStem, fake, st);
}

static uint32_t peelToStructTypeId(spirv_cross::Compiler& comp, uint32_t typeId)
{
    const auto& t = comp.get_type(typeId);

    // If it’s directly a struct, return.
    if (t.basetype == spirv_cross::SPIRType::Struct)
        return t.self;

    // If it’s an array, SPIRV-Cross keeps basetype, but .array is non-empty.
    // We need to peel arrays by clearing .array and asking for a type with same "self" is not possible.
    // Instead: the member_types[] IDs already point at the correct type objects; so to detect
    // array-of-struct, check comp.get_type(typeId).basetype after clearing array is not possible.
    // Practical approach: if basetype is Struct we’re done; otherwise return 0.
    return 0;
}

static void emitStructDepsRecursive(
    spirv_cross::Compiler& comp,
    uint32_t typeId,
    std::unordered_set<uint32_t>& emitted,
    const std::function<void(uint32_t)>& emitStructFn)
{
    const auto& t = comp.get_type(typeId);
    if (t.basetype != spirv_cross::SPIRType::Struct)
        return;

    // Walk members
    for (uint32_t i = 0; i < (uint32_t)t.member_types.size(); ++i)
    {
        const uint32_t memberTypeId = t.member_types[i];
        const auto& mt = comp.get_type(memberTypeId);

        // Direct nested struct
        if (mt.basetype == spirv_cross::SPIRType::Struct)
        {
            if (!emitted.contains(mt.self))
            {
                emitStructDepsRecursive(comp, mt.self, emitted, emitStructFn);
                emitStructFn(mt.self);
                emitted.insert(mt.self);
            }
            continue;
        }

        // Array-of-struct case: SPIRV-Cross still reports basetype == Struct for arrays-of-struct
        // in typical builds, but if your build differs, you can extend here by inspecting the
        // element type more deeply. In most Vulkan GLSL->SPIR-V cases this path is unnecessary.
    }
}


static std::string stemFromPath(const std::string& p)
{
    // crude stem extractor: take last component, strip extension(s)
    std::string file = p;
    auto slash = file.find_last_of("/\\");
    if (slash != std::string::npos) file = file.substr(slash + 1);

    // Remove trailing ".spv" if present
    if (file.size() > 4 && file.substr(file.size() - 4) == ".spv")
        file = file.substr(0, file.size() - 4);

    // Replace '.' with '_' to form an identifier stem ("cube.vert" -> "cube_vert")
    for (char& c : file)
        if (c == '.') c = '_';

    file = sanitizeIdent(file, "shader");
    return file;
}

int main(int argc, char** argv)
{
    try
    {
        if (argc < 3)
        {
            std::cerr << "Usage: ShaderStructGen <input.spv> <output.h>\n";
            return 1;
        }

        const std::string inPath = argv[1];
        const std::string outPath = argv[2];

        const std::vector<uint32_t> spirv = readSpirvWords(inPath);
        spirv_cross::Compiler comp(spirv);

        const auto resources = comp.get_shader_resources();

        std::ofstream os(outPath, std::ios::binary);
        if (!os)
            throw std::runtime_error("Failed to open output: " + outPath);

        const std::string guard = "PNKR_SHADERGEN_" + toUpperIdent(outPath);

        os << "#pragma once\n\n";
        os << "#include <array>\n";
        os << "#include <cstddef>\n";
        os << "#include <cstdint>\n";
        os << "#include <type_traits>\n";
        os << "#include \"pnkr/renderer/shadergen_common.hpp\"\n\n";

        os << "namespace ShaderGen {\n";

        const std::string shaderStem = stemFromPath(inPath);

        auto emitBlockList = [&](const char* label, auto const& list)
        {
            for (const auto& res : list)
            {
                const auto& st = comp.get_type(res.base_type_id);
                if (st.basetype != spirv_cross::SPIRType::Struct)
                    continue;

                // 1) Emit nested struct dependencies first (PerDrawData, etc.)
                emitStructDepsRecursive(
                    comp,
                    st.self,
                    emittedStructTypes,
                    [&](uint32_t depTypeId)
                    {
                        emitStructByTypeId(os, comp, depTypeId);
                    });

                // 2) Emit the block struct itself (possibly renamed via shaderStem heuristic)
                emitStruct(os, comp, shaderStem, res, st);
            }
        };


        // Push constants, UBOs, SSBOs
        emitBlockList("push", resources.push_constant_buffers);
        emitBlockList("ubo", resources.uniform_buffers);
        emitBlockList("ssbo", resources.storage_buffers);

        os << "} // namespace ShaderGen\n";

        std::cout << "[ShaderStructGen] Processing: " << inPath << "\n";
        std::cout << "[ShaderStructGen] Success -> " << outPath << "\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ShaderStructGen] Error: " << e.what() << "\n";
        return 2;
    }
}
