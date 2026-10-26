#version 450 core

// Open ocean at sunset. Use the Screen preview mesh (Alt+4); drag in the
// viewport to look around.
//
// Channels (see the Channels panel):
//   uChannel0  RGBA Noise 256       foam break-up
//   uChannel1  Water Normals 512    small ripples on top of the swell
//   uChannel2  Clouds 512           sky
//
// The swell is a sum of sharp-crested directional waves. Each wave moves at
// the deep-water speed for its wavelength (omega = sqrt(g k)), and nudges the
// point where the next waves are evaluated along its slope, which bunches
// them into crests and flattens the troughs. Units are metres.

uniform vec2 uResolution;
uniform float uTime;
uniform vec4 uMouse;
uniform sampler2D uChannel0;
uniform sampler2D uChannel1;
uniform sampler2D uChannel2;

uniform float uSunHeight = 0.10;                 // @range(-0.05, 1) @label(Sun height)
uniform float uSunDirection = 0.0;               // @range(-3.14, 3.14) @label(Sun direction)
uniform float uWaveHeight = 2.8;                 // @range(0.1, 6) @label(Wave height)
uniform float uChoppiness = 0.55;                // @range(0, 1.2) @label(Choppiness)
uniform float uWind = 0.6;                       // @range(-3.14, 3.14) @label(Wind direction)
uniform float uFoam = 1.0;                       // @range(0, 3) @label(Foam)
uniform vec3 uDeepColor = vec3(0.005, 0.03, 0.05);   // @color @label(Deep water)
uniform vec3 uScatterColor = vec3(0.04, 0.24, 0.22); // @color @label(Scattering)
uniform float uCameraHeight = 4.0;               // @range(1, 40) @label(Camera height)
uniform float uExposure = 0.8;                   // @range(0.3, 3) @label(Exposure)

out vec4 fragColor;

const float kGravity = 9.81;
const float kFar = 3000.0;
const int kTraceWaves = 14;   // waves used to find the surface
const int kDetailWaves = 34;  // waves used for normals

// Swell height in [0, 1] (multiply by uWaveHeight).
float swell(vec2 p, int waves)
{
    float k = 0.08;      // wavenumber of the longest wave (~80 m)
    float weight = 1.0;
    float height = 0.0;
    float total = 0.0;
    for (int i = 0; i < waves; ++i) {
        float angle = uWind + 1.1 * sin(float(i) * 2.39996); // spread around the wind
        vec2 dir = vec2(cos(angle), sin(angle));
        float phase = dot(dir, p) * k - uTime * sqrt(kGravity * k);
        float wave = exp(sin(phase) - 1.0);   // sharp crests, wide troughs
        float slope = wave * cos(phase);
        p -= dir * slope * weight * uChoppiness / k;
        height += wave * weight;
        total += weight;
        weight *= 0.80;
        k *= 1.21;
    }
    return height / total;
}

float seaLevel(vec2 xz, int waves)
{
    return (swell(xz, waves) - 1.0) * uWaveHeight;
}

// Distance to the sea surface along the ray, or -1 if the ray misses it.
float traceSea(vec3 ro, vec3 rd)
{
    if (rd.y >= 0.0)
        return -1.0;
    // The surface lies between y = -uWaveHeight and y = 0.
    float t = max((0.0 - ro.y) / rd.y, 0.0);
    float tEnd = (-uWaveHeight - ro.y) / rd.y;
    float previousT = t;
    float previousD = 1.0;
    for (int i = 0; i < 160 && t < tEnd; ++i) {
        vec3 p = ro + rd * t;
        float d = p.y - seaLevel(p.xz, kTraceWaves);
        if (d < 0.0) {
            // Crossed the surface: refine between the last two samples.
            return mix(previousT, t, previousD / (previousD - d));
        }
        previousT = t;
        previousD = d;
        t += max(d * 0.9, 0.004 * t);
    }
    return min(t, tEnd);
}

vec3 sunDirection()
{
    float elevation = uSunHeight * 1.4;
    return normalize(vec3(sin(uSunDirection) * cos(elevation), sin(elevation), cos(uSunDirection) * cos(elevation)));
}

vec3 sky(vec3 rd, vec3 sun)
{
    float up = max(rd.y, 0.0);
    float s = max(dot(rd, sun), 0.0);
    float sunset = 1.0 - smoothstep(0.0, 0.45, sun.y);
    vec3 zenith = mix(vec3(0.10, 0.24, 0.52), vec3(0.05, 0.08, 0.20), sunset);
    vec3 horizon = mix(vec3(0.55, 0.66, 0.80), vec3(0.90, 0.42, 0.18), sunset);
    vec3 color = mix(horizon, zenith, pow(up, 0.3));
    color += vec3(1.0, 0.40, 0.10) * 0.35 * pow(s, 8.0) * (0.25 + sunset);
    color += vec3(1.0, 0.75, 0.45) * 0.7 * pow(s, 120.0);

    if (rd.y > 0.0) {
        vec2 uv = rd.xz / (rd.y + 0.05) * 0.12 + vec2(uTime * 0.003, 0.0);
        float cover = texture(uChannel2, uv).r * 0.75 + texture(uChannel2, uv * 3.1 + 0.37).r * 0.25;
        cover = smoothstep(0.35, 0.9, cover) * smoothstep(0.0, 0.15, rd.y);
        vec3 lit = mix(vec3(1.0, 0.55, 0.30), vec3(1.0, 0.95, 0.90), 1.0 - sunset);
        vec3 cloud = mix(zenith * 1.4 + vec3(0.10, 0.06, 0.08) * sunset, lit, 0.2 + 0.8 * pow(s, 4.0));
        color = mix(color, cloud, cover * 0.9);
    }
    // Sun disk (behind clouds is fine; it is very bright).
    color += vec3(1.0, 0.85, 0.6) * 40.0 * smoothstep(0.99988, 0.99994, s);
    return color;
}

vec3 seaNormal(vec3 p, float t)
{
    float e = 0.02 + 0.0015 * t;
    float h = seaLevel(p.xz, kDetailWaves);
    vec3 n = normalize(vec3(h - seaLevel(p.xz + vec2(e, 0.0), kDetailWaves),
                            e,
                            h - seaLevel(p.xz + vec2(0.0, e), kDetailWaves)));
    // Ripples from the normal map, two layers drifting with the wind; they
    // fade out with distance where they would only shimmer.
    vec2 wind = vec2(cos(uWind), sin(uWind));
    vec3 r1 = texture(uChannel1, p.xz * 0.09 + wind * uTime * 0.02).xyz * 2.0 - 1.0;
    vec3 r2 = texture(uChannel1, p.xz * 0.23 - wind.yx * uTime * 0.03).xyz * 2.0 - 1.0;
    vec2 ripple = (r1.xy + r2.xy) * 0.12 * exp(-t * 0.015);
    return normalize(n + vec3(ripple.x, 0.0, ripple.y));
}

float foam(vec3 p, float crest)
{
    vec2 q = p.xz * 0.6;
    float breakup = texture(uChannel0, q / 256.0 * 4.0).r * 0.6 + texture(uChannel0, q / 256.0 * 13.0).g * 0.4;
    return smoothstep(0.78, 0.98, crest + 0.25 * (breakup - 0.5)) * uFoam;
}

vec3 shadeSea(vec3 p, vec3 rd, float t, vec3 sun)
{
    vec3 n = seaNormal(p, t);
    float cosTheta = max(dot(n, -rd), 0.0);
    float fresnel = 0.02 + 0.98 * pow(1.0 - cosTheta, 5.0);
    fresnel = min(fresnel, 0.9); // clamp the grazing-angle mirror look

    vec3 reflected = reflect(rd, n);
    reflected.y = abs(reflected.y);
    vec3 reflection = sky(reflected, sun);

    // Light scattered back out of the water, strongest in wave crests seen
    // against the sun.
    float crest = clamp((p.y + uWaveHeight) / uWaveHeight, 0.0, 1.0);
    float towardsSun = pow(max(dot(normalize(vec3(rd.x, 0.0, rd.z)), normalize(vec3(sun.x, 0.0, sun.z))), 0.0), 2.0);
    float sunLight = clamp(sun.y * 4.0 + 0.3, 0.0, 1.0);
    vec3 water = uDeepColor + uScatterColor * pow(crest, 2.5) * (0.35 + 1.4 * towardsSun) * sunLight;
    water += uScatterColor * 0.12 * max(dot(n, sun), 0.0) * sunLight;

    vec3 color = mix(water, reflection, fresnel);
    vec3 sunColor = mix(vec3(1.0, 0.55, 0.25), vec3(1.0, 0.95, 0.85), smoothstep(0.0, 0.5, sun.y));
    color += sunColor * 18.0 * pow(max(dot(reflected, sun), 0.0), 900.0) * sunLight;  // glint
    color += sunColor * 0.5 * pow(max(dot(reflected, sun), 0.0), 40.0) * sunLight;    // glitter path

    float f = foam(p, crest) * exp(-t * 0.004);
    color = mix(color, vec3(0.75, 0.80, 0.82) * (0.25 + 0.75 * sunLight), f);

    // Haze towards the horizon.
    float haze = 1.0 - exp(-t * 0.0011);
    return mix(color, sky(normalize(vec3(rd.x, 0.002, rd.z)), sun), haze);
}

void main()
{
    vec2 uv = (2.0 * gl_FragCoord.xy - uResolution) / uResolution.y;
    vec3 sun = sunDirection();

    vec3 ro = vec3(0.0, uCameraHeight, uTime * 1.5);
    float yaw = uSunDirection + 0.25;
    float pitch = -0.06;
    if (uMouse.x > 0.0) { // drag to look around
        yaw += (uMouse.x / uResolution.x - 0.5) * 4.0;
        pitch += (uMouse.y / uResolution.y - 0.5) * 1.2;
    }
    vec3 forward = vec3(sin(yaw) * cos(pitch), sin(pitch), cos(yaw) * cos(pitch));
    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), forward));
    vec3 up = cross(forward, right);
    vec3 rd = normalize(forward * 1.8 + right * uv.x + up * uv.y);

    vec3 color;
    float t = traceSea(ro, rd);
    if (t > 0.0 && t < kFar)
        color = shadeSea(ro + rd * t, rd, t, sun);
    else
        color = sky(rd.y < 0.0 ? normalize(vec3(rd.x, 0.002, rd.z)) : rd, sun);

    color = 1.0 - exp(-color * uExposure);
    vec2 q = gl_FragCoord.xy / uResolution;
    color *= 0.65 + 0.35 * pow(16.0 * q.x * q.y * (1.0 - q.x) * (1.0 - q.y), 0.12);
    fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
