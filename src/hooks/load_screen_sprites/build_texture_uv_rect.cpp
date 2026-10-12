// build_texture_uv_rect, retail 0x003713A0. See the header for the description, evidence and status
// (draft, oracle pending). All arithmetic goes through the fork's EE float model so the four stored
// bit patterns match the console exactly.

#include "hooks/load_screen_sprites/build_texture_uv_rect.hpp"

#include <ps2/fpu.hpp>

#include <utility>

namespace b3::load_screen_sprites {

bool build_texture_uv_rect(std::uint32_t *out_uv, std::uint32_t flip_flags,
                           std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h,
                           std::uint32_t tex_w, std::uint32_t tex_h) {
    namespace fp = ps2::fp;

    // The far edges are the near edges plus the extent, formed with the EE's truncating addition.
    const std::uint32_t x_right  = fp::add(x, w);
    const std::uint32_t y_bottom = fp::add(y, h);

    // Each edge becomes a texture coordinate by division by the texture size. The retail order is
    // v_bottom, u_left, v_top, u_right; it only matters for which division first raises the flag,
    // and the flag is a single sticky condition, so any order gives the same outcome.
    const fp::Result v_bottom = fp::div(y_bottom, tex_h);
    const fp::Result u_left   = fp::div(x, tex_w);
    const fp::Result v_top    = fp::div(y, tex_h);
    const fp::Result u_right  = fp::div(x_right, tex_w);

    std::uint32_t u0 = u_left.value, u1 = u_right.value;
    std::uint32_t v0 = v_top.value, v1 = v_bottom.value;
    if (flip_flags & 1u) std::swap(u0, u1);   // horizontal mirror
    if (flip_flags & 2u) std::swap(v0, v1);   // vertical mirror

    out_uv[0] = u0;
    out_uv[1] = v0;
    out_uv[2] = u1;
    out_uv[3] = v1;

    return v_bottom.invalid || u_left.invalid || v_top.invalid || u_right.invalid;
}

}
