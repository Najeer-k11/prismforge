#version 450 core

// Full-screen "aurora" for wallpapers and loops. Use the Screen preview mesh.
// Coordinates come from gl_FragCoord and uResolution, so the pattern keeps its
// proportions at any output size or aspect ratio (including portrait).

uniform vec2 uResolution;
uniform float uTime;

uniform vec3 uSkyTop = vec3(0.01, 0.02, 0.06);      // @color @label(Sky top)
uniform vec3 uSkyBottom = vec3(0.05, 0.03, 0.12);   // @color @label(Sky bottom)
uniform vec3 uAuroraA = vec3(0.10, 0.95, 0.65);     // @color @label(Aurora A)
uniform vec3 uAuroraB = vec3(0.55, 0.25, 1.00);     // @color @label(Aurora B)
uniform float uIntensity = 1.2;                     // @range(0, 3) @label(Intensity)
uniform float uScale = 1.6;                         // @range(0.3, 5) @label(Scale)
uniform float uSpeed = 0.08;                        // @range(0, 0.5) @label(Speed)
uniform float uStars = 1.0;                         // @range(0, 2) @label(Stars)

out vec4 fragColor;

float hash(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

float fbm(vec2 p)
{
    float sum = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 6; ++i) {
        sum += amplitude * noise(p);
        p = p * 2.03 + vec2(11.7, 3.1);
        amplitude *= 0.5;
    }
    return sum;
}

void main()
{
    // Height-normalised coordinates: y in [0, 1], x scaled by aspect ratio.
    vec2 uv = gl_FragCoord.xy / uResolution.y;
    float t = uTime * uSpeed;

    vec3 color = mix(uSkyBottom, uSkyTop, smoothstep(0.0, 1.0, uv.y));

    // Stars: sparse cells, twinkling slowly.
    vec2 cell = floor(uv * 180.0);
    float star = step(0.9965, hash(cell)) * (0.6 + 0.4 * sin(uTime * 2.0 + hash(cell + 7.0) * 6.28));
    color += vec3(star) * uStars * smoothstep(0.25, 0.8, uv.y);

    // Aurora curtains: domain-warped noise bands.
    vec2 p = uv * uScale;
    vec2 warp = vec2(fbm(p + vec2(t, 0.0)), fbm(p + vec2(0.0, t) + 5.2));
    float bands = fbm(vec2(p.x * 1.5 + warp.x * 2.0 + t, p.y * 0.35 + warp.y));
    float curtain = smoothstep(0.35, 0.75, bands) * smoothstep(0.15, 0.55, uv.y) * smoothstep(1.05, 0.6, uv.y);
    vec3 aurora = mix(uAuroraA, uAuroraB, smoothstep(0.35, 0.95, uv.y + warp.x * 0.3));
    color += aurora * curtain * uIntensity;

    // Soft vignette and gentle tone map.
    vec2 centered = gl_FragCoord.xy / uResolution - 0.5;
    color *= 1.0 - 0.35 * dot(centered, centered);
    color = color / (1.0 + color);
    fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
