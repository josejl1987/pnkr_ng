#ifndef PHYSICS_SHARED_SLANG
#define PHYSICS_SHARED_SLANG

struct PhysicsVertex {
    float3 position;
    float _pad0;
    float3 startPosition;
    float _pad1;
    float3 previousPosition;
    float _pad2;
    float3 normal;
    uint jointCount;
    float3 velocity;
    float mass;
    float3 force;
    float _pad3;
    uint joints[12];
};

struct PhysicsSceneData {
    float3 windDirection;
    uint resetSimulation;
    float airDensity;
    float springStiffness;
    float springDamping;
    float _pad;
};

#endif
