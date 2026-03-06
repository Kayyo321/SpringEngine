#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 u_screen_size;
uniform float u_time;
uniform float u_pixel_size;
uniform float u_vignette_inner;
uniform float u_vignette_outer;
uniform float u_edge_glow;
uniform float u_pulse_speed;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main() {
    vec2 uv = fragTexCoord;
    vec2 centered = uv - vec2(0.5);

    float radius = length(centered) * 1.41421356;
    float edge_mask = smoothstep(u_vignette_inner, u_vignette_outer, radius);
    edge_mask = pow(edge_mask, 0.72);

    vec2 pixel_step = vec2(max(1.0, u_pixel_size)) / max(u_screen_size, vec2(1.0));
    vec2 quantized_uv = floor(uv / pixel_step) * pixel_step + (pixel_step * 0.5);

    vec4 base_color = texture(texture0, uv);
    vec4 pixel_color = texture(texture0, quantized_uv);

    float pulse = 0.5 + 0.5 * sin((u_time * u_pulse_speed) + (radius * 19.0));
    vec3 glow_tint = vec3(0.06, 0.28, 0.46) * pulse * u_edge_glow;

    float frame_phase = floor(u_time * 24.0);
    vec2 noise_cell = floor(quantized_uv * u_screen_size / max(1.0, u_pixel_size));
    float r_noise = hash21(noise_cell + vec2(frame_phase, 7.0));
    float g_noise = hash21(noise_cell + vec2(13.0, frame_phase));
    float b_noise = hash21(noise_cell + vec2(frame_phase * 0.5, frame_phase * 0.25));

    vec3 crt_tint = vec3(r_noise, g_noise, b_noise);
    crt_tint = (crt_tint - 0.5) * 0.48;

    vec3 mixed_rgb = mix(base_color.rgb, pixel_color.rgb, edge_mask);
    mixed_rgb *= (1.0 - (edge_mask * 0.34));
    mixed_rgb += glow_tint * edge_mask;
    mixed_rgb += crt_tint * edge_mask;

    finalColor = vec4(mixed_rgb, base_color.a) * colDiffuse * fragColor;
}
