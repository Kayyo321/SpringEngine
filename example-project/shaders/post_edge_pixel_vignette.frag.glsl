#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 u_screen_size;
uniform float u_time;
uniform float u_pixel_size;      // controls scanline density (higher = coarser)
uniform float u_vignette_inner;  // radius where CRT edge effects begin
uniform float u_vignette_outer;  // radius where they reach full strength
uniform float u_edge_glow;       // chromatic aberration + phosphor mask strength
uniform float u_pulse_speed;     // rolling scanline bar + flicker speed
uniform float u_intensity;       // 0 = clean passthrough, 1 = full CRT

// Barrel/pincushion distortion — the classic CRT curved-glass look.
// k > 0 bows the image outward at the corners.
vec2 crt_curve(vec2 uv, float k) {
    vec2 c = (uv - 0.5) * 2.0;
    c *= 1.0 + dot(c, c) * k;
    return c * 0.5 + 0.5;
}

void main() {
    vec2 uv = fragTexCoord;
    float intensity = clamp(u_intensity, 0.0, 1.0);

    // --- Barrel distortion (scales with intensity so 0 = flat) ---
    vec2 wuv = crt_curve(uv, 0.10 * intensity);

    // Soft black border past the warped screen boundary (the bezel)
    vec2 bound     = smoothstep(0.0, 0.015, wuv) * smoothstep(1.0, 0.985, wuv);
    float in_screen = bound.x * bound.y;

    // --- Edge mask (0 at center, 1 at corners) ---
    vec2  centered  = uv - 0.5;
    float radius    = length(centered) * 1.41421356;
    float edge_mask = smoothstep(u_vignette_inner, u_vignette_outer, radius);

    // --- Chromatic aberration ---
    // R/B channels drift apart along the radial direction toward edges,
    // mimicking phosphor convergence error on old CRTs.
    float aberr  = edge_mask * u_edge_glow * 0.016;
    vec2  ab_dir = normalize(centered + vec2(0.00001));
    float r_ch   = texture(texture0, wuv + ab_dir * aberr).r;
    float g_ch   = texture(texture0, wuv).g;
    float b_ch   = texture(texture0, wuv - ab_dir * aberr).b;
    vec3  crt_rgb = vec3(r_ch, g_ch, b_ch);

    // --- Scanlines ---
    // Horizontal dark bands every ~2 pixels.  u_pixel_size raises line spacing.
    float scan_freq = (u_screen_size.y * 3.14159 * 0.5) / max(u_pixel_size, 1.0);
    float scanline  = pow(abs(sin(wuv.y * scan_freq)), 1.4);
    // Scanlines are subtle in the centre; they deepen toward the edges.
    float scan_str  = mix(0.08, 0.45, edge_mask) * intensity;
    crt_rgb        *= mix(1.0, scanline, scan_str);

    // --- Rolling dim bar ---
    // Slow-drifting dim stripe — the visible refresh sweep of old CRT tubes.
    float roll_t   = fract(wuv.y - u_time * u_pulse_speed * 0.03);
    float roll_bar = 1.0 - smoothstep(0.0, 0.06, roll_t) * 0.14 * intensity;
    crt_rgb       *= roll_bar;

    // --- Phosphor column mask ---
    // Faint R/G/B column tint replicating the sub-pixel stripe pattern of a
    // shadow-mask CRT.  Only visible near the edges where u_edge_glow pushes it.
    float col = mod(floor(wuv.x * u_screen_size.x), 3.0);
    vec3  phosphor = vec3(1.0);
    phosphor.r += step(col, 0.5)             * 0.13;
    phosphor.g += step(abs(col - 1.0), 0.5) * 0.13;
    phosphor.b += step(abs(col - 2.0), 0.5) * 0.13;
    crt_rgb *= mix(vec3(1.0), phosphor, edge_mask * u_edge_glow * 0.75);

    // --- Vignette ---
    // Strong corner darkening — CRT phosphor coatings always dimmed at the edges.
    crt_rgb *= 1.0 - edge_mask * 0.72;

    // --- Subtle flicker ---
    // Very slight whole-frame luminance pulse (imperceptible but adds life).
    float flicker = 1.0 - 0.018 * abs(sin(u_time * u_pulse_speed * 6.7)) * intensity;
    crt_rgb *= flicker;

    // Black out anything past the curved screen boundary
    crt_rgb *= in_screen;

    // --- Intensity blend: 0 = clean passthrough, 1 = full CRT ---
    vec4 base_color = texture(texture0, uv);
    vec3 final_rgb  = mix(base_color.rgb, crt_rgb, intensity);

    finalColor = vec4(final_rgb, base_color.a) * colDiffuse * fragColor;
}
