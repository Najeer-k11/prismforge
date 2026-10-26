#version 450 core

// Flight over procedural mountains. Use the Screen preview mesh (Alt+4) and
// drag in the viewport to look around.
//
// Channels (see the Channels panel):
//   uChannel0  RGBA Noise 256   terrain shape, read with texelFetch
//   uChannel1  Rock 512         cliffs (triplanar)
//   uChannel2  Clouds 512       sky
//   uChannel3  Ground 512       grass and dirt in the valleys
//
// The terrain is fractal value noise whose octaves are damped where the
// slope accumulated so far is steep, which gives eroded-looking ridges and
// smooth valleys.

uniform vec2 uResolution;
uniform float uTime;
uniform vec4 uMouse;
uniform sampler2D uChannel0;
uniform sampler2D uChannel1;
uniform sampler2D uChannel2;
uniform sampler2D uChannel3;

uniform float uSunHeight = 0.22;    // @range(0.02, 1) @label(Sun height)
uniform float uSunDirection = 0.9;  // @range(-3.14, 3.14) @label(Sun direction)
uniform float uSnowLine = 0.55;     // @range(0, 1.2) @label(Snow line)
uniform float uHaze = 1.0;          // @range(0, 4) @label(Haze)
uniform float uSpeed = 1.0;         // @range(0, 4) @label(Flight speed)
uniform float uAltitude = 1.0;      // @range(0.3, 4) @label(Altitude)
uniform float uExposure = 1.0;      // @range(0.3, 3) @label(Exposure)

out vec4 fragColor;

const float kTerrainScale = 320.0;   // size of the largest features
const float kTerrainHeight = 260.0;  // height of the tallest peaks
const float kFar = 7000.0;
const mat2 kOctave = mat2(1.6, -1.2, 1.2, 1.6); // rotate by ~37° and scale by 2

// Value noise and its analytic derivative from 2×2 noise texels.
vec3 noised(vec2 x)
{
    ivec2 i = ivec2(floor(x));
    vec2 f = fract(x);
    vec2 u = f * f * (3.0 - 2.0 * f);
    vec2 du = 6.0 * f * (1.0 - f);
    float a = texelFetch(uChannel0, (i) & 255, 0).x;
    float b = texelFetch(uChannel0, (i + ivec2(1, 0)) & 255, 0).x;
    float c = texelFetch(uChannel0, (i + ivec2(0, 1)) & 255, 0).x;
    float d = texelFetch(uChannel0, (i + ivec2(1, 1)) & 255, 0).x;
    float k = a - b - c + d;
    return vec3(a + (b - a) * u.x + (c - a) * u.y + k * u.x * u.y,
                du * (vec2(b - a, c - a) + k * u.yx));
}

float terrain(vec2 xz, int octaves)
{
    vec2 p = xz / kTerrainScale;
    float h = 0.0;
    float amplitude = 0.5;
    vec2 slope = vec2(0.0);
    for (int i = 0; i < octaves; ++i) {
        vec3 n = noised(p);
        slope += n.yz;
        h += amplitude * n.x / (1.0 + dot(slope, slope));
        amplitude *= 0.5;
        p = kOctave * p;
    }
    // Broaden the valleys a little.
    return kTerrainHeight * h * smoothstep(0.0, 0.7, h) * 1.25;
}

vec3 terrainNormal(vec2 xz, float distance)
{
    float e = max(0.0015 * distance, 0.05);
    const int kOctaves = 13;
    return normalize(vec3(terrain(xz - vec2(e, 0.0), kOctaves) - terrain(xz + vec2(e, 0.0), kOctaves),
                          2.0 * e,
                          terrain(xz - vec2(0.0, e), kOctaves) - terrain(xz + vec2(0.0, e), kOctaves)));
}

float march(vec3 ro, vec3 rd)
{
    float t = 1.0;
    for (int i = 0; i < 260; ++i) {
        vec3 p = ro + rd * t;
        float d = p.y - terrain(p.xz, 8);
        if (d < 0.002 * t || t > kFar)
            break;
        t += 0.45 * d;
    }
    return t;
}

float softShadow(vec3 ro, vec3 rd)
{
    float light = 1.0;
    float t = 1.0;
    for (int i = 0; i < 40; ++i) {
        vec3 p = ro + rd * t;
        float h = p.y - terrain(p.xz, 6);
        light = min(light, 12.0 * h / t);
        t += clamp(h, 2.0, 60.0);
        if (light < 0.0 || p.y > kTerrainHeight * 1.5)
            break;
    }
    return smoothstep(0.0, 1.0, light);
}

vec3 sunDirection()
{
    float elevation = uSunHeight * 1.4;
    return normalize(vec3(sin(uSunDirection) * cos(elevation), sin(elevation), cos(uSunDirection) * cos(elevation)));
}

vec3 sky(vec3 rd, vec3 sun)
{
    float up = max(rd.y, 0.0);
    vec3 color = mix(vec3(0.66, 0.75, 0.86), vec3(0.20, 0.38, 0.70), pow(up, 0.45));
    float s = max(dot(rd, sun), 0.0);
    color += vec3(1.0, 0.65, 0.35) * 0.30 * pow(s, 6.0);
    color += vec3(1.0, 0.92, 0.75) * 6.0 * pow(s, 900.0);

    // A cloud layer: project the ray onto a plane high above.
    if (rd.y > 0.0) {
        vec2 uv = rd.xz / rd.y * 0.18 + vec2(uTime * 0.004, 0.0);
        float cover = texture(uChannel2, uv).r * 0.7 + texture(uChannel2, uv * 2.7 + 0.3).r * 0.3;
        cover = smoothstep(0.25, 0.85, cover) * smoothstep(0.0, 0.25, rd.y);
        vec3 cloud = mix(vec3(0.72, 0.76, 0.84), vec3(1.0, 0.95, 0.88), 0.5 + 0.5 * pow(s, 4.0));
        color = mix(color, cloud, cover * 0.85);
    }
    // Haze towards the horizon.
    return mix(color, vec3(0.72, 0.77, 0.83), pow(1.0 - up, 10.0) * 0.7);
}

// Triplanar mapping: no stretching on cliffs.
vec3 triplanar(sampler2D tex, vec3 p, vec3 n, float scale)
{
    vec3 w = pow(abs(n), vec3(4.0));
    w /= w.x + w.y + w.z;
    return texture(tex, p.zy * scale).rgb * w.x + texture(tex, p.xz * scale).rgb * w.y +
           texture(tex, p.xy * scale).rgb * w.z;
}

vec3 shadeTerrain(vec3 p, vec3 n, vec3 rd, float t, vec3 sun)
{
    float height = p.y / kTerrainHeight;
    vec3 rock = triplanar(uChannel1, p, n, 0.025);
    rock *= vec3(0.62, 0.55, 0.48) * (0.6 + 0.8 * triplanar(uChannel1, p, n, 0.0031).r); // large-scale variation
    vec3 grass = texture(uChannel3, p.xz * 0.04).rgb * vec3(0.9, 1.0, 0.8);

    float detail = texture(uChannel2, p.xz * 0.003).r;
    float grassMask = smoothstep(0.82, 0.92, n.y) * (1.0 - smoothstep(0.15, 0.35, height + 0.15 * detail));
    vec3 albedo = mix(rock, grass, grassMask);

    float snow = smoothstep(uSnowLine - 0.08, uSnowLine + 0.04, height + 0.12 * detail + 0.25 * (n.y - 0.8));
    snow *= smoothstep(0.45, 0.75, n.y);
    albedo = mix(albedo, vec3(0.92, 0.94, 0.98), snow);

    float diffuse = max(dot(n, sun), 0.0);
    float shadow = diffuse > 0.0 ? softShadow(p + n * 0.5, sun) : 0.0;
    float skyLight = 0.5 + 0.5 * n.y;
    vec3 bounceDir = normalize(vec3(-sun.x, 0.0, -sun.z));
    float bounce = max(dot(n, bounceDir), 0.0) * clamp(1.0 - height, 0.0, 1.0);

    vec3 light = diffuse * shadow * vec3(1.0, 0.86, 0.68) * 2.6;
    light += skyLight * vec3(0.24, 0.34, 0.52) * 0.8;
    light += bounce * vec3(0.35, 0.28, 0.22) * 0.35;
    vec3 color = albedo * light;

    // Glint on snow.
    vec3 h = normalize(sun - rd);
    color += snow * shadow * vec3(1.0, 0.9, 0.8) * 0.6 * pow(max(dot(n, h), 0.0), 40.0);

    // Height-dependent aerial perspective, brighter towards the sun.
    float fog = 1.0 - exp(-uHaze * 0.00028 * t * exp(-max(p.y, 0.0) * 0.004));
    vec3 fogColor = mix(vec3(0.58, 0.67, 0.80), vec3(1.0, 0.82, 0.62), pow(max(dot(rd, sun), 0.0), 6.0));
    return mix(color, fogColor, fog);
}

void main()
{
    vec2 uv = (2.0 * gl_FragCoord.xy - uResolution) / uResolution.y;
    vec3 sun = sunDirection();

    // The camera follows a winding path, staying above the ground ahead.
    float z = uTime * 28.0 * uSpeed;
    vec2 here = vec2(260.0 * sin(z * 0.0021) + 90.0 * sin(z * 0.0047), z);
    vec2 ahead = vec2(260.0 * sin((z + 40.0) * 0.0021) + 90.0 * sin((z + 40.0) * 0.0047), z + 40.0);
    float ground = 0.5 * (terrain(here, 4) + terrain(mix(here, ahead, 3.0), 4));
    vec3 ro = vec3(here.x, ground + 70.0 * uAltitude, here.y);
    ro.y = max(ro.y, terrain(here, 8) + 18.0);

    float yaw = atan(ahead.x - here.x, ahead.y - here.y);
    float pitch = -0.12;
    if (uMouse.x > 0.0) { // drag to look around
        yaw += (uMouse.x / uResolution.x - 0.5) * 3.5;
        pitch += (uMouse.y / uResolution.y - 0.5) * 1.5;
    }
    vec3 forward = vec3(sin(yaw) * cos(pitch), sin(pitch), cos(yaw) * cos(pitch));
    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), forward));
    vec3 up = cross(forward, right);
    vec3 rd = normalize(forward * 1.6 + right * uv.x + up * uv.y);

    vec3 color;
    float t = march(ro, rd);
    if (t < kFar) {
        vec3 p = ro + rd * t;
        color = shadeTerrain(p, terrainNormal(p.xz, t), rd, t, sun);
    } else {
        color = sky(rd, sun);
    }

    // Filmic tone map, grade and vignette.
    color = 1.0 - exp(-color * uExposure);
    color = pow(color, vec3(0.9, 0.95, 1.0));
    vec2 q = gl_FragCoord.xy / uResolution;
    color *= 0.6 + 0.4 * pow(16.0 * q.x * q.y * (1.0 - q.x) * (1.0 - q.y), 0.15);
    fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
