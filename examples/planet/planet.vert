#version 450 core

// PrismForge vertex interface
//   attributes: 0 = position, 1 = normal, 2 = uv (object space)
//   uniforms:   uModel, uView, uProjection, uNormalMatrix, uCameraPosition,
//               uTime (seconds), uResolution (framebuffer pixels)

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec2 vUV;

void main()
{
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPosition = world.xyz;
    vWorldNormal = normalize(uNormalMatrix * aNormal);
    vUV = aUV;
    gl_Position = uProjection * uView * world;
}
