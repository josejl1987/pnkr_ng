struct FSInput {
    float4 color : COLOR;
};

[shader("fragment")]
float4 fragmentMain(FSInput input) : SV_Target {
    return input.color;
}
