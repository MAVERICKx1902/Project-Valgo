#include "d3d12_shaders.h"

namespace valgo::rhi::d3d12 {
namespace {

constexpr const char* kShaderLibrary = R"hlsl(
// ---------------------------------------------------------------------------
// Shared resources
// ---------------------------------------------------------------------------
cbuffer FrameBuffer : register(b1)
{
    float4x4 gViewProjection;
    float4x4 gView;
    float4x4 gProjection;
    float4x4 gInverseViewProjection;
    float4   gCameraPosition;
    float4   gLightDirection;
    float4   gLightColor;
    float4   gAmbientSky;
    float4   gAmbientGround;
    float4   gFogColor;
    float4   gFogParameters;
    float4   gViewport;
    float4   gClearColor;
    float4   gSkyZenith;
    float4   gSkyHorizon;
};

cbuffer DrawBuffer : register(b0)
{
    float4x4 gWorld;
    float4   gColor;
    float4   gParams;   // x = unlit, y = texture weight, z = emissive, w = tiling
};

Texture2D    gBaseColor : register(t0);
SamplerState gSampler   : register(s0);

float3 SrgbToLinear3(float3 c)
{
    // Exact sRGB transfer function, matching valgo::SrgbToLinear in math.h.
    float3 low = c / 12.92;
    float3 high = pow(saturate((c + 0.055) / 1.055), 2.4);
    float3 selector = step(0.04045, c);
    return lerp(low, high, selector);
}

float3 ApplyFog(float3 rgb, float3 world_position)
{
    if (gFogParameters.w < 0.5)
    {
        return rgb;
    }
    float distance_to_camera = length(world_position - gCameraPosition.xyz);
    float denom = max(gFogParameters.y - gFogParameters.x, 1e-3);
    float t = saturate((distance_to_camera - gFogParameters.x) / denom);
    return lerp(rgb, gFogColor.rgb, t);
}

float3 ShadeLit(float3 albedo, float3 normal, float3 world_position)
{
    float3 to_camera = normalize(gCameraPosition.xyz - world_position);
    if (dot(normal, to_camera) < 0.0)
    {
        normal = -normal;  // two-sided lighting, matches the software backend
    }
    float3 light_direction = normalize(gLightDirection.xyz);
    float ndl = saturate(dot(normal, light_direction)) * gLightDirection.w;
    float3 hemisphere = lerp(gAmbientGround.rgb, gAmbientSky.rgb, normal.y * 0.5 + 0.5);
    float fill = saturate(dot(normal, to_camera)) * 0.20;
    return albedo * (hemisphere + gAmbientSky.rgb * fill + gLightColor.rgb * ndl);
}

float3 SampleAlbedo(float3 base_color, float2 uv)
{
    float weight = saturate(gParams.y);
    if (weight <= 0.001)
    {
        return base_color;
    }
    float3 texel = gBaseColor.Sample(gSampler, uv).rgb;   // sRGB texture format
    return lerp(base_color, base_color * texel, weight);
}

// ---------------------------------------------------------------------------
// Lit / unlit meshes
// ---------------------------------------------------------------------------
struct MeshVSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct MeshVSOutput
{
    float4 clip_position : SV_Position;
    float3 world_normal  : NORMAL;
    float3 world_position: TEXCOORD1;
    float2 uv            : TEXCOORD0;
};

MeshVSOutput VSMesh(MeshVSInput input)
{
    MeshVSOutput output;
    float4 world = mul(gWorld, float4(input.position, 1.0));
    output.world_position = world.xyz;
    output.world_normal = normalize(mul((float3x3)gWorld, input.normal));
    output.clip_position = mul(gViewProjection, world);
    output.uv = input.uv;
    return output;
}

float4 PSMeshLit(MeshVSOutput input) : SV_Target
{
    float3 albedo = SampleAlbedo(gColor.rgb, input.uv);
    float3 rgb = ShadeLit(albedo, normalize(input.world_normal), input.world_position);

    float unlit = saturate(gParams.x);
    rgb = lerp(rgb, albedo, unlit);
    rgb += albedo * gParams.z;
    rgb = ApplyFog(rgb, input.world_position);
    return float4(rgb, gColor.a);
}

float4 PSMeshUnlit(MeshVSOutput input) : SV_Target
{
    float3 albedo = SampleAlbedo(gColor.rgb, input.uv);
    return float4(albedo + albedo * gParams.z, gColor.a);
}

// ---------------------------------------------------------------------------
// Sky: full-screen triangle reconstructed from the inverse view projection
// ---------------------------------------------------------------------------
struct SkyVSOutput
{
    float4 clip_position : SV_Position;
};

SkyVSOutput VSSky(uint vertex_id : SV_VertexID)
{
    // Oversized triangle covering the viewport.
    float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    SkyVSOutput output;
    output.clip_position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    // Y flip so the gradient is not upside down.
    output.clip_position.y = -output.clip_position.y;
    return output;
}

float4 PSSky(SkyVSOutput input) : SV_Target
{
    float2 ndc = input.clip_position.xy;
    float4 near_world = mul(gInverseViewProjection, float4(ndc, 0.0, 1.0));
    float4 far_world  = mul(gInverseViewProjection, float4(ndc, 1.0, 1.0));
    if (abs(near_world.w) < 1e-6 || abs(far_world.w) < 1e-6)
    {
        return float4(gSkyHorizon.rgb, 1.0);
    }
    float3 near_point = near_world.xyz / near_world.w;
    float3 far_point  = far_world.xyz / far_world.w;
    float3 direction  = normalize(far_point - near_point);

    float up = direction.y;
    float3 horizon = gSkyHorizon.rgb;
    float3 zenith = gSkyZenith.rgb;
    float3 ground = horizon * float3(0.18, 0.19, 0.21);

    float3 rgb;
    if (up >= 0.0)
    {
        float t = pow(saturate(up), 0.65);
        rgb = lerp(horizon, zenith, t);
    }
    else
    {
        float t = pow(saturate(-up), 0.5);
        rgb = lerp(horizon, ground, t);
    }

    float3 light_direction = normalize(gLightDirection.xyz);
    float sun = saturate(dot(direction, light_direction));
    rgb += gLightColor.rgb * (pow(sun, 64.0) * 0.55 + pow(sun, 8.0) * 0.08);
    return float4(rgb, 1.0);
}

// ---------------------------------------------------------------------------
// Debug lines: R32G32B32 position + R8G8B8A8 packed colour (authored sRGB)
// ---------------------------------------------------------------------------
struct LineVSInput
{
    float3 position : POSITION;
    uint   color    : COLOR;
};

struct LineVSOutput
{
    float4 clip_position : SV_Position;
    float4 color         : COLOR;
};

LineVSOutput VSLine(LineVSInput input)
{
    LineVSOutput output;
    output.clip_position = mul(gViewProjection, float4(input.position, 1.0));
    float4 display = float4(
        (input.color & 0xFF) / 255.0,
        ((input.color >> 8) & 0xFF) / 255.0,
        ((input.color >> 16) & 0xFF) / 255.0,
        ((input.color >> 24) & 0xFF) / 255.0);
    output.color = float4(SrgbToLinear3(display.rgb), display.a);
    return output;
}

float4 PSLine(LineVSOutput input) : SV_Target
{
    return input.color;
}

// ---------------------------------------------------------------------------
// Screen-space overlay: pixel coordinates, uv, packed sRGB colour
// ---------------------------------------------------------------------------
struct ScreenVSInput
{
    float2 position : POSITION;
    float2 uv       : TEXCOORD0;
    uint   color    : COLOR;
};

struct ScreenVSOutput
{
    float4 clip_position : SV_Position;
    float4 color         : COLOR;
    float2 uv            : TEXCOORD0;
};

ScreenVSOutput VSScreen(ScreenVSInput input)
{
    ScreenVSOutput output;
    float2 ndc = float2(input.position.x / max(gViewport.x, 1.0) * 2.0 - 1.0,
                        1.0 - input.position.y / max(gViewport.y, 1.0) * 2.0);
    output.clip_position = float4(ndc, 0.0, 1.0);
    float4 display = float4(
        (input.color & 0xFF) / 255.0,
        ((input.color >> 8) & 0xFF) / 255.0,
        ((input.color >> 16) & 0xFF) / 255.0,
        ((input.color >> 24) & 0xFF) / 255.0);
    output.color = float4(SrgbToLinear3(display.rgb), display.a);
    output.uv = input.uv;
    return output;
}

float4 PSScreen(ScreenVSOutput input) : SV_Target
{
    return input.color;
}
)hlsl";

}  // namespace

std::string_view VertexEntryPoint(ShaderProgram program) {
    switch (program) {
        case ShaderProgram::LitMesh:
        case ShaderProgram::UnlitMesh: return "VSMesh";
        case ShaderProgram::DebugLine: return "VSLine";
        case ShaderProgram::Sky: return "VSSky";
        case ShaderProgram::Screen2D: return "VSScreen";
        case ShaderProgram::Count: break;
    }
    return "VSMesh";
}

std::string_view PixelEntryPoint(ShaderProgram program) {
    switch (program) {
        case ShaderProgram::LitMesh: return "PSMeshLit";
        case ShaderProgram::UnlitMesh: return "PSMeshUnlit";
        case ShaderProgram::DebugLine: return "PSLine";
        case ShaderProgram::Sky: return "PSSky";
        case ShaderProgram::Screen2D: return "PSScreen";
        case ShaderProgram::Count: break;
    }
    return "PSMeshLit";
}

const char* GetShaderLibrary() { return kShaderLibrary; }

}  // namespace valgo::rhi::d3d12
