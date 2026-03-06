#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float u_time;
uniform float u_distort_speed;
uniform float u_distort_frequency;
uniform float u_distort_strength;

void main() {
    vec2 uv = fragTexCoord;
    vec2 p  = uv * u_distort_frequency;
    float t = u_time * u_distort_speed;

    // 2D domain warp: two sine pairs per axis at irrational angle ratios
    // (sqrt(3) ~ 1.732, 1+sqrt(2) ~ 2.414, 1/sqrt(3) ~ 0.577).
    // Mixing non-axis-aligned directions at different speeds breaks the
    // grid symmetry that causes boxy-looking squiggles.
    float dx = sin(p.x          + p.y * 1.732 + t)
             + sin(p.x * 2.414  - p.y * 0.577 + t * 1.37) * 0.5;
    float dy = sin(p.x * 1.732  + p.y         + t * 0.89)
             + sin(-p.x * 0.577 + p.y * 2.414 + t * 1.13) * 0.5;

    uv += vec2(dx, dy) * (u_distort_strength * 0.45);

    vec4 texel = texture(texture0, uv);
    if (texel.a < 0.01)
        discard;

    // Arcane shimmer: smooth noise blend between violet and electric blue
    vec2 sp = uv * 9.0;
    float shimmerN = sin(sp.x          + sp.y * 1.732 + t * 0.6)
                   + sin(sp.x * 1.732  - sp.y         + t * 0.8);
    shimmerN = shimmerN * 0.25 + 0.5;

    vec3 shimmerCol = mix(
        vec3(0.35, 0.04, 0.80),  // deep violet
        vec3(0.05, 0.55, 1.00),  // electric blue
        shimmerN
    ) * 0.55;

    vec3 shaded = (texel.rgb + shimmerCol) * colDiffuse.rgb * fragColor.rgb;
    finalColor = vec4(shaded, texel.a * colDiffuse.a * fragColor.a);
}
