#include "pnkr/renderer/gpu_shared/SkinningShared.h"

[[vk::push_constant]] ConstantBuffer<SkinningPushConstants> g_Push;

void writeOutputVertex(uint idx, float3 pos, float3 norm, float4 tang) {
    g_Push.outVertices[idx].position = float4(pos, 1.0);
    g_Push.outVertices[idx].normal   = float4(normalize(norm), 0.0);
    g_Push.outVertices[idx].tangent  = float4(normalize(tang.xyz), tang.w);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint idx = dispatchThreadID.x;
    if (idx >= g_Push.vertexCount) return;

    // Load Input Vertex
    SkinVertexIn v = g_Push.inVertices[idx];

    // Pass-through attributes
    g_Push.outVertices[idx].color   = v.color;
    g_Push.outVertices[idx].uv0     = v.uv0;
    g_Push.outVertices[idx].uv1     = v.uv1;

    // Use swizzle because VertexGPU uses float4 for position/normal/tangent
    float3 pos  = v.position.xyz;
    float3 norm = v.normal.xyz;
    float3 tang = v.tangent.xyz;

    // Morph Targets
    if (g_Push.hasMorphing != 0 && g_Push.morphDeltas != nullptr && g_Push.morphStates != nullptr) {
        // Bounds check: ensure v.meshIndex is within the morphStates array
        if (v.meshIndex < g_Push.numMorphStates) {
            // Use v.meshIndex to find the correct morph state for this vertex
            MorphState ms = g_Push.morphStates[v.meshIndex];
            
            // Only apply morphing if this mesh has valid morph target data
            if (ms.meshIndex != ~0u) {
                for (int i = 0; i < 8; ++i) {
                    if (ms.weights[i] != 0.0) {
                        // MorphVertex is now VertexGPU, assuming deltas are in pos/norm/tan
                        // Accessing morphDeltas array using stored offset + vertex index
                        MorphVertex mv = g_Push.morphDeltas[ms.activeTargets[i] + v.localIndex];
                        
                        pos  += mv.position.xyz * ms.weights[i];
                        norm += mv.normal.xyz * ms.weights[i];
                        tang += mv.tangent.xyz * ms.weights[i];
                    }
                }
            }
        }
    }

    // Skinning
    if (g_Push.hasSkinning != 0) {
        float4x4 jointMat = 
            v.weights.x * g_Push.jointMatrices[v.joints.x] +
            v.weights.y * g_Push.jointMatrices[v.joints.y] +
            v.weights.z * g_Push.jointMatrices[v.joints.z] +
            v.weights.w * g_Push.jointMatrices[v.joints.w];

        // Check if skinned (has any weights)
        if (dot(v.weights, float4(1.0)) > 0.0) {
            // Apply Skin Matrix (To World Space)
            float3 worldPos  = mul(jointMat, float4(pos, 1.0)).xyz;
            float3 worldNorm = normalize(mul((float3x3)jointMat, norm));
            float3 worldTang = normalize(mul((float3x3)jointMat, tang));

            // Transform BACK to Local Space to avoid double-transformation in VS
            if (g_Push.meshXforms != nullptr) {
                // Now using meshIndex to find the correct transform for this vertex
                MeshXform mx = g_Push.meshXforms[v.meshIndex];
                g_Push.outVertices[idx].position = float4(mul(mx.invModel, float4(worldPos, 1.0)).xyz, 1.0);
                g_Push.outVertices[idx].normal   = float4(normalize(mul((float3x3)mx.normalWorldToLocal, worldNorm)), 0.0);
                g_Push.outVertices[idx].tangent  = float4(normalize(mul((float3x3)mx.normalWorldToLocal, worldTang)), v.tangent.w);
            } else {
                 g_Push.outVertices[idx].position = float4(worldPos, 1.0);
                 g_Push.outVertices[idx].normal   = float4(worldNorm, 0.0);
                 g_Push.outVertices[idx].tangent  = float4(worldTang, v.tangent.w);
            }
        } else {
            writeOutputVertex(idx, pos, norm, v.tangent);
        }
    } else {
        // No Skinning, just Morphing (if any)
        writeOutputVertex(idx, pos, norm, v.tangent);
    }
    
    // Copy other members that might be needed
    g_Push.outVertices[idx].joints = v.joints;
    g_Push.outVertices[idx].weights = v.weights;
    g_Push.outVertices[idx].meshIndex = v.meshIndex;
}
