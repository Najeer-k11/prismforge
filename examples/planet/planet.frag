#version 450 core

// A small rotating planet: textures on a regular mesh. Use the Sphere preview
// mesh and orbit with the mouse.
//
// Channels (see the Channels panel):
//   uChannel0  Fractal Noise 512   continents and mountains (triplanar)
//   uChannel1  Clouds 512          cloud layer, wrapped with the sphere's UVs
//   uChannel2  Ground 512          land colour detail
//   uChannel3  Voronoi Cells 256   city lights on the night side
//
// Terrain is mapped triplanar from the surface direction, so it has no seam
// and no pinching at the poles. Clouds use vUV directly: the textures tile,
// so the u = 0/1 seam is invisible.

in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec2 vUV;

uniform vec3 uCameraPosition;
uniform float uTime;
uniform sampler2D uChannel0;
uniform sampler2D uChannel1;
uniform sampler2D uChannel2;
uniform sampler2D uChannel3;

uniform float uSeaLevel = 0.52;                  // @range(0.2, 0.8) @label(Sea level)
uniform float uRotationSpeed = 0.05;             // @range(0, 0.5) @label(Rotation speed)
uniform float uCloudCover = 0.7;                 // @range(0, 1) @label(Cloud cover)
uniform float uSunAngle = 1.9;                   // @range(-3.14, 3.14) @label(Sun direction)
uniform vec3 uOceanColor = vec3(0.02, 0.10, 0.22);   // @color @label(Ocean)
uniform vec3 uAtmosphere = vec3(0.35, 0.60, 1.0);    // @color @label(Atmosphere)
uniform float uCityLights = 1.0;                 // @range(0, 3) @label(City lights)

out vec4 fragColor;

mat3 rotationY(float a)
{
    float c = cos(a), s = sin(a);
    return mat3(c, 0.0, -s, 0.0, 1.0, 0.0, s, 0.0, c);
}

// `lod` picks a blurred mip level: large, smooth shapes from a detailed texture.
float triplanar(sampler2D tex, vec3 dir, float scale, float lod)
{
    vec3 w = pow(abs(dir), vec3(6.0));
    w /= w.x + w.y + w.z;
    return textureLod(tex, dir.zy * scale, lod).r * w.x + textureLod(tex, dir.xz * scale + 0.31, lod).r * w.y +
           textureLod(tex, dir.xy * scale + 0.67, lod).r * w.z;
}

// Height in [0, 1]: big continents plus ridged detail.
float height(vec3 dir)
{
    float continents = triplanar(uChannel0, dir, 0.45, 4.0);
    float detail = triplanar(uChannel0, dir, 1.2, 2.0);
    float ridges = 1.0 - abs(triplanar(uChannel0, dir, 3.1, 1.5) * 2.0 - 1.0);
    return continents * 0.7 + detail * 0.22 + ridges * ridges * 0.08;
}

void main()
{
    vec3 n = normalize(vWorldNormal);
    vec3 v = normalize(uCameraPosition - vWorldPosition);
    vec3 sun = normalize(vec3(sin(uSunAngle), 0.25, cos(uSunAngle)));

    // Surface direction in the planet's rotating frame.
    float spin = uTime * uRotationSpeed;
    vec3 dir = rotationY(spin) * normalize(vWorldPosition);

    float h = height(dir);
    float land = smoothstep(uSeaLevel, uSeaLevel + 0.01, h);
    float elevation = clamp((h - uSeaLevel) / (1.0 - uSeaLevel), 0.0, 1.0);
    float latitude = abs(dir.y);

    // Bump the land normal from the height gradient along two tangents.
    vec3 tangent = normalize(cross(vec3(0.0, 1.0, 0.0), dir + vec3(1e-4, 0.0, 0.0)));
    vec3 bitangent = cross(dir, tangent);
    const float e = 0.004;
    float hT = height(normalize(dir + tangent * e));
    float hB = height(normalize(dir + bitangent * e));
    vec3 bumpLocal = normalize(dir - (tangent * (hT - h) + bitangent * (hB - h)) * 0.025 / e);
    vec3 surfaceNormal = normalize(mix(n, transpose(rotationY(spin)) * bumpLocal, land));

    // Longitude/latitude in the rotating frame, for small-scale detail.
    vec2 sphereUV = vec2(atan(dir.x, dir.z) / 6.2831853, asin(clamp(dir.y, -1.0, 1.0)) / 3.1415927);

    // Land colours: green lowlands, dry belts, brown highlands, snow.
    vec3 detail = texture(uChannel2, sphereUV * vec2(2.0, 1.0), 1.0).rgb;
    float dryness = smoothstep(0.15, 0.35, 0.35 - latitude + 0.25 * (triplanar(uChannel0, dir, 1.3, 3.0) - 0.5));
    vec3 lowland = mix(detail * vec3(0.8, 1.0, 0.7), vec3(0.62, 0.52, 0.34) * (0.7 + 0.6 * detail.r), dryness);
    vec3 landColor = mix(lowland, vec3(0.36, 0.30, 0.24), smoothstep(0.25, 0.6, elevation));
    float snow = smoothstep(0.62, 0.7, elevation + latitude * 0.5) + smoothstep(0.82, 0.88, latitude);
    landColor = mix(landColor, vec3(0.92, 0.95, 1.0), clamp(snow, 0.0, 1.0));

    // Water: shallow shelves are lighter.
    float depth = clamp((uSeaLevel - h) / uSeaLevel * 6.0, 0.0, 1.0);
    vec3 water = mix(uOceanColor * 1.8 + vec3(0.0, 0.05, 0.05), uOceanColor, depth);
    float ice = smoothstep(0.86, 0.9, latitude);
    water = mix(water, vec3(0.85, 0.9, 0.95), ice);
    vec3 albedo = mix(water, landColor, land);

    // Clouds on the sphere's UVs, drifting a little faster than the ground.
    vec2 cloudUV = vUV * vec2(2.0, 1.0) + vec2(spin * 0.18 + uTime * 0.004, 0.0);
    float clouds = texture(uChannel1, cloudUV).r * 0.7 + texture(uChannel1, cloudUV * 2.3 + 0.5).r * 0.3;
    clouds = smoothstep(1.0 - uCloudCover, 1.15 - uCloudCover * 0.6, clouds);
    clouds *= smoothstep(0.98, 0.85, vUV.y) * smoothstep(0.02, 0.15, vUV.y); // thin out at the poles

    float ndl = dot(surfaceNormal, sun);
    float daylight = smoothstep(-0.12, 0.25, dot(n, sun));
    float diffuse = max(ndl, 0.0);
    vec3 color = albedo * (diffuse * vec3(1.0, 0.95, 0.88) * 1.6 + 0.02) * (1.0 - clouds * 0.45);

    // Sun glint on the oceans.
    vec3 halfway = normalize(sun + v);
    color += (1.0 - land) * (1.0 - ice) * vec3(1.0, 0.9, 0.75) * pow(max(dot(n, halfway), 0.0), 120.0) * daylight;

    color = mix(color, vec3(1.0) * (max(dot(n, sun), 0.0) * 1.5 + 0.02), clouds);

    // City lights where it is night, on low land.
    vec3 cells = texture(uChannel3, sphereUV * vec2(6.0, 3.0)).rgb;
    float cities = smoothstep(0.12, 0.0, cells.r) * step(0.55, cells.b) * land * (1.0 - snow) *
                   (1.0 - smoothstep(0.1, 0.4, elevation));
    color += vec3(1.0, 0.7, 0.35) * cities * (1.0 - daylight) * (1.0 - clouds * 0.8) * uCityLights;

    // Atmosphere: scattering towards the limb, lit side only.
    float limb = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    color += uAtmosphere * limb * (0.15 + 1.2 * smoothstep(-0.2, 0.6, dot(n, sun)));

    color = 1.0 - exp(-color * 1.3);
    fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
