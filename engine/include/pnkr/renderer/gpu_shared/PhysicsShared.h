#pragma once

#include "SlangCppBridge.h"

#ifdef __cplusplus
#include <glm/glm.hpp>
namespace gpu {
#endif

struct PhysicsVertex {
    float3 position;
    float3 start_position;
    float3 previous_position;
    float3 normal;
    uint joint_count;
    float3 velocity;
    float mass;
    float3 force;
    uint joints[ 12 ];
};

struct PhysicsSceneData {
    float3 windDirection;
    uint resetSimulation;
    float airDensity;
    float springStiffness;
    float springDamping;
    float _pad0;
    float _pad1;
    float _pad2;
};

#ifdef __cplusplus
}
#endif
