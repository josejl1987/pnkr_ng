#include "pnkr/assets/GeometryProcessor.hpp"
#include "pnkr/core/logger.hpp"
#include <mikktspace.h>
#include <glm/vec4.hpp>

namespace pnkr::assets {

    namespace {
        struct TangentContext {
            ImportedPrimitive* m_primitive;

            static int getNumFaces(const SMikkTSpaceContext* context) {
                auto* user = static_cast<TangentContext*>(context->m_pUserData);
                return (int)(user->m_primitive->indices.size() / 3);
            }

            static int getNumVerticesOfFace(const SMikkTSpaceContext*, const int) {
                return 3;
            }

            static void getPosition(const SMikkTSpaceContext* context, float fvPosOut[], const int iFace, const int iVert) {
                auto* user = static_cast<TangentContext*>(context->m_pUserData);
                uint32_t index = user->m_primitive->indices[(iFace * 3) + iVert];
                const auto& pos = user->m_primitive->vertices[index].position;
                fvPosOut[0] = pos.x;
                fvPosOut[1] = pos.y;
                fvPosOut[2] = pos.z;
            }

            static void getNormal(const SMikkTSpaceContext* context, float fvNormOut[], const int iFace, const int iVert) {
                auto* user = static_cast<TangentContext*>(context->m_pUserData);
                uint32_t index = user->m_primitive->indices[(iFace * 3) + iVert];
                const auto& norm = user->m_primitive->vertices[index].normal;
                fvNormOut[0] = norm.x;
                fvNormOut[1] = norm.y;
                fvNormOut[2] = norm.z;
            }

            static void getTexCoord(const SMikkTSpaceContext* context, float fvTexcOut[], const int iFace, const int iVert) {
                auto* user = static_cast<TangentContext*>(context->m_pUserData);
                uint32_t index = user->m_primitive->indices[(iFace * 3) + iVert];
                const auto& uv = user->m_primitive->vertices[index].uv0;
                fvTexcOut[0] = uv.x;
                fvTexcOut[1] = uv.y;
            }

            static void setTSpaceBasic(const SMikkTSpaceContext* context, const float fvTangent[], const float fSign, const int iFace, const int iVert) {
                auto* user = static_cast<TangentContext*>(context->m_pUserData);
                uint32_t index = user->m_primitive->indices[(iFace * 3) + iVert];
                user->m_primitive->vertices[index].tangent = glm::vec4(fvTangent[0], fvTangent[1], fvTangent[2], fSign);
            }
        };
    }

    void GeometryProcessor::generateTangents(ImportedPrimitive& prim) {
        TangentContext userContext{};
        userContext.m_primitive = &prim;

        SMikkTSpaceInterface iface{};
        iface.m_getNumFaces = TangentContext::getNumFaces;
        iface.m_getNumVerticesOfFace = TangentContext::getNumVerticesOfFace;
        iface.m_getPosition = TangentContext::getPosition;
        iface.m_getNormal = TangentContext::getNormal;
        iface.m_getTexCoord = TangentContext::getTexCoord;
        iface.m_setTSpaceBasic = TangentContext::setTSpaceBasic;

        SMikkTSpaceContext context{};
        context.m_pInterface = &iface;
        context.m_pUserData = &userContext;

        core::Logger::Scene.info("Generating tangents for mesh with {} vertices and {} indices.", prim.vertices.size(), prim.indices.size());
        genTangSpaceDefault(&context);
    }

}
