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

    float wave = sin((uv.y * u_distort_frequency) + (u_time * u_distort_speed));
    uv.x += wave * u_distort_strength;

    float pulse = sin((uv.x + uv.y + (u_time * 1.3)) * 12.0) * 0.5 + 0.5;
    uv.y += (pulse - 0.5) * u_distort_strength * 0.35;

    vec4 texel = texture(texture0, uv);
    if (texel.a < 0.01)
        discard;

    vec3 shimmer = vec3(0.22, 0.05, 0.35) * (0.5 + 0.5 * sin((u_time * 4.0) + (uv.y * 20.0)));
    vec3 shaded = (texel.rgb + shimmer) * colDiffuse.rgb * fragColor.rgb;

    finalColor = vec4(shaded, texel.a * colDiffuse.a * fragColor.a);
}
