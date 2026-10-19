#version 450 core

// PrismForge default material: a simple lit surface with an animated UV grid.
// Edit and press F5 (or enable auto-compile) to see changes.

in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec2 vUV;

uniform vec3 uCameraPosition;
uniform float uTime;

out vec4 fragColor;

const vec3 kLightDir = normalize(vec3(0.6, 0.8, 0.4));

float grid(vec2 uv, float cells)
{
    vec2 g = abs(fract(uv * cells - 0.5) - 0.5) / fwidth(uv * cells);
    return 1.0 - min(min(g.x, g.y), 1.0);
}

void main()
{
    vec3 n = normalize(vWorldNormal);
    vec3 v = normalize(uCameraPosition - vWorldPosition);
    vec3 h = normalize(kLightDir + v);

    vec3 base = mix(vec3(0.18, 0.42, 0.62), vec3(0.85, 0.48, 0.22),
                    0.5 + 0.5 * sin(vUV.x * 6.2831 + uTime));
    base = mix(base, vec3(0.95), 0.35 * grid(vUV, 8.0));

    float diffuse = max(dot(n, kLightDir), 0.0);
    float specular = pow(max(dot(n, h), 0.0), 64.0);
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);

    vec3 color = base * (0.08 + 0.92 * diffuse) + vec3(0.6) * specular + vec3(0.25, 0.35, 0.45) * rim;
    fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
