// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4D - GX register-write proxy shim (capture backend B).
 *
 * Include this header BEFORE <nds.h>:
 *
 *     #include "twlhd_gx_shim.hpp"
 *     #include <nds.h>
 *
 * Rationale: libnds' gl* helpers are `static inline` (GL_STATIC_INL) in
 * <nds/arm9/videoGL.h> and expand the GFX_* / MATRIX_* port macros when that
 * header is parsed. Redefining the macros after <nds.h> would be too late, so
 * the shim includes <nds/arm9/video.h> itself (making the later inclusion a
 * no-op thanks to its include guard), then replaces every write-capable port
 * macro with a proxy object that records the write and performs the real MMIO
 * access.
 *
 * TWLHD_SHIM_HW_READ(addr, width) / TWLHD_SHIM_HW_WRITE(addr, width, value)
 * are the hardware seam. Define them before including this header for host
 * tests with mocked registers; the defaults perform real volatile accesses.
 *
 * Read-only ports are deliberately left untouched: GFX_STATUS,
 * GFX_RDLINES_COUNT, GFX_VERTEX_RAM_USAGE, GFX_POLYGON_RAM_USAGE,
 * GFX_POS_RESULT, GFX_VEC_RESULT, MATRIX_READ_CLIP, MATRIX_READ_VECTOR,
 * POINT_RESULT, VECTOR_RESULT and GFX_BUSY.
 *
 * Not supported: the packed display-list macros (FIFO_* / REG2ID) and
 * glCallList(). The proxy scalar deletes operator& so REG2ID(GFX_*) fails to
 * compile loudly instead of silently producing a host pointer value.
 *
 * Everything here is HARDWARE UNVERIFIED: the host shim test executes real
 * libnds header code with mocked hardware. It is not a DS run.
 */
#pragma once

#include <stdint.h>

#include <nds/arm9/video.h>

#ifdef __cplusplus
namespace twlhd_shim {

static inline uint32_t default_read(uint32_t addr, uint32_t width) {
    switch (width) {
    case 1u:
        return *reinterpret_cast<volatile uint8_t *>(addr);
    case 2u:
        return *reinterpret_cast<volatile uint16_t *>(addr);
    default:
        return *reinterpret_cast<volatile uint32_t *>(addr);
    }
}

static inline void default_write(uint32_t addr, uint32_t width,
                                 uint32_t value) {
    switch (width) {
    case 1u:
        *reinterpret_cast<volatile uint8_t *>(addr) = (uint8_t)value;
        break;
    case 2u:
        *reinterpret_cast<volatile uint16_t *>(addr) = (uint16_t)value;
        break;
    default:
        *reinterpret_cast<volatile uint32_t *>(addr) = value;
        break;
    }
}

} /* namespace twlhd_shim */
#endif

#ifndef TWLHD_SHIM_HW_READ
#define TWLHD_SHIM_HW_READ(addr, width) \
    ::twlhd_shim::default_read((addr), (width))
#endif

#ifndef TWLHD_SHIM_HW_WRITE
#define TWLHD_SHIM_HW_WRITE(addr, width, value) \
    ::twlhd_shim::default_write((addr), (width), (value))
#endif

#include "twlhd_gx_record.h"

namespace twlhd_shim {

static inline uint32_t mask_to_width(uint32_t value, uint32_t width) {
    switch (width) {
    case 1u:
        return value & 0xFFu;
    case 2u:
        return value & 0xFFFFu;
    default:
        return value;
    }
}

template <uint32_t ADDR, uint32_t WIDTH>
class Port {
public:
    Port() {}

    Port &operator=(uint32_t value) {
        uint32_t masked = mask_to_width(value, WIDTH);
        twlhd_gx_rec_hook(ADDR, masked, WIDTH);
        TWLHD_SHIM_HW_WRITE(ADDR, WIDTH, masked);
        return *this;
    }

    Port &operator=(const Port &) = delete;

    operator uint32_t() const { return TWLHD_SHIM_HW_READ(ADDR, WIDTH); }

    Port &operator|=(uint32_t bits) {
        return *this = (static_cast<uint32_t>(*this) | bits);
    }

    Port &operator&=(uint32_t bits) {
        return *this = (static_cast<uint32_t>(*this) & bits);
    }

    /* Deliberately unavailable: REG2ID(GFX_*) must not silently miscompile. */
    Port *operator&() = delete;
};

template <uint32_t ADDR, uint32_t WIDTH>
class ArrayPort {
public:
    class Element {
    public:
        Element(ArrayPort &owner, uint32_t index)
            : owner_(owner), index_(index) {}

        Element &operator=(uint32_t value) {
            owner_.write(index_, value);
            return *this;
        }

        Element &operator=(const Element &) = delete;

        operator uint32_t() const { return owner_.read(index_); }

        Element &operator|=(uint32_t bits) {
            return *this = (static_cast<uint32_t>(*this) | bits);
        }

        Element &operator&=(uint32_t bits) {
            return *this = (static_cast<uint32_t>(*this) & bits);
        }

        Element *operator&() = delete;

    private:
        ArrayPort &owner_;
        uint32_t index_;
    };

    ArrayPort() {}

    Element operator[](uint32_t index) { return Element(*this, index); }

    /* No implicit conversion to a raw pointer: pointer-style use fails. */
    const void *operator&() = delete;

private:
    uint32_t read(uint32_t index) const {
        return TWLHD_SHIM_HW_READ(ADDR + index * WIDTH, WIDTH);
    }

    void write(uint32_t index, uint32_t value) {
        uint32_t addr = ADDR + index * WIDTH;
        uint32_t masked = mask_to_width(value, WIDTH);
        twlhd_gx_rec_hook(addr, masked, WIDTH);
        TWLHD_SHIM_HW_WRITE(addr, WIDTH, masked);
    }
};

template <uint32_t ADDR, uint32_t WIDTH>
Port<ADDR, WIDTH> port() {
    return Port<ADDR, WIDTH>();
}

template <uint32_t ADDR, uint32_t WIDTH>
ArrayPort<ADDR, WIDTH> array_port() {
    return ArrayPort<ADDR, WIDTH>();
}

} /* namespace twlhd_shim */

/* --- 3D control / FIFO / vertex attribute ports ------------------------- */
#undef GFX_CONTROL
#define GFX_CONTROL (::twlhd_shim::port<0x04000060u, 2u>())

#undef GFX_FIFO
#define GFX_FIFO (::twlhd_shim::port<0x04000400u, 4u>())

#undef GFX_COLOR
#define GFX_COLOR (::twlhd_shim::port<0x04000480u, 4u>())

#undef GFX_VERTEX10
#define GFX_VERTEX10 (::twlhd_shim::port<0x04000490u, 4u>())

#undef GFX_VERTEX_XY
#define GFX_VERTEX_XY (::twlhd_shim::port<0x04000494u, 4u>())

#undef GFX_VERTEX_XZ
#define GFX_VERTEX_XZ (::twlhd_shim::port<0x04000498u, 4u>())

#undef GFX_VERTEX_YZ
#define GFX_VERTEX_YZ (::twlhd_shim::port<0x0400049Cu, 4u>())

#undef GFX_VERTEX_DIFF
#define GFX_VERTEX_DIFF (::twlhd_shim::port<0x040004A0u, 4u>())

#undef GFX_VERTEX16
#define GFX_VERTEX16 (::twlhd_shim::port<0x0400048Cu, 4u>())

#undef GFX_TEX_COORD
#define GFX_TEX_COORD (::twlhd_shim::port<0x04000488u, 4u>())

#undef GFX_TEX_FORMAT
#define GFX_TEX_FORMAT (::twlhd_shim::port<0x040004A8u, 4u>())

#undef GFX_PAL_FORMAT
#define GFX_PAL_FORMAT (::twlhd_shim::port<0x040004ACu, 4u>())

#undef GFX_NORMAL
#define GFX_NORMAL (::twlhd_shim::port<0x04000484u, 4u>())

#undef GFX_LIGHT_VECTOR
#define GFX_LIGHT_VECTOR (::twlhd_shim::port<0x040004C8u, 4u>())

#undef GFX_LIGHT_COLOR
#define GFX_LIGHT_COLOR (::twlhd_shim::port<0x040004CCu, 4u>())

#undef GFX_DIFFUSE_AMBIENT
#define GFX_DIFFUSE_AMBIENT (::twlhd_shim::port<0x040004C0u, 4u>())

#undef GFX_SPECULAR_EMISSION
#define GFX_SPECULAR_EMISSION (::twlhd_shim::port<0x040004C4u, 4u>())

#undef GFX_SHININESS
#define GFX_SHININESS (::twlhd_shim::port<0x040004D0u, 4u>())

#undef GFX_POLY_FORMAT
#define GFX_POLY_FORMAT (::twlhd_shim::port<0x040004A4u, 4u>())

#undef GFX_BEGIN
#define GFX_BEGIN (::twlhd_shim::port<0x04000500u, 4u>())

#undef GFX_END
#define GFX_END (::twlhd_shim::port<0x04000504u, 4u>())

#undef GFX_FLUSH
#define GFX_FLUSH (::twlhd_shim::port<0x04000540u, 4u>())

#undef GFX_VIEWPORT
#define GFX_VIEWPORT (::twlhd_shim::port<0x04000580u, 4u>())

#undef GFX_BOX_TEST
#define GFX_BOX_TEST (::twlhd_shim::port<0x040005C0u, 4u>())

#undef GFX_POS_TEST
#define GFX_POS_TEST (::twlhd_shim::port<0x040005C4u, 4u>())

#undef GFX_VEC_TEST
#define GFX_VEC_TEST (::twlhd_shim::port<0x040005C8u, 4u>())

/* --- rendering-engine state (RSTA) -------------------------------------- */
#undef GFX_CLEAR_COLOR
#define GFX_CLEAR_COLOR (::twlhd_shim::port<0x04000350u, 4u>())

#undef GFX_CLEAR_DEPTH
#define GFX_CLEAR_DEPTH (::twlhd_shim::port<0x04000354u, 2u>())

#undef GFX_CLRIMAGE_OFFSET
#define GFX_CLRIMAGE_OFFSET (::twlhd_shim::port<0x04000356u, 2u>())

#undef GFX_ALPHA_TEST
#define GFX_ALPHA_TEST (::twlhd_shim::port<0x04000340u, 2u>())

#undef GFX_FOG_COLOR
#define GFX_FOG_COLOR (::twlhd_shim::port<0x04000358u, 4u>())

#undef GFX_FOG_OFFSET
#define GFX_FOG_OFFSET (::twlhd_shim::port<0x0400035Cu, 4u>())

#undef GFX_CUTOFF_DEPTH
#define GFX_CUTOFF_DEPTH (::twlhd_shim::port<0x04000610u, 2u>())

#undef GFX_TOON_TABLE
#define GFX_TOON_TABLE (::twlhd_shim::array_port<0x04000380u, 2u>())

#undef GFX_EDGE_TABLE
#define GFX_EDGE_TABLE (::twlhd_shim::array_port<0x04000330u, 2u>())

#undef GFX_FOG_TABLE
#define GFX_FOG_TABLE (::twlhd_shim::array_port<0x04000360u, 1u>())

/* --- matrix processor ports --------------------------------------------- */
#undef MATRIX_CONTROL
#define MATRIX_CONTROL (::twlhd_shim::port<0x04000440u, 4u>())

#undef MATRIX_PUSH
#define MATRIX_PUSH (::twlhd_shim::port<0x04000444u, 4u>())

#undef MATRIX_POP
#define MATRIX_POP (::twlhd_shim::port<0x04000448u, 4u>())

#undef MATRIX_SCALE
#define MATRIX_SCALE (::twlhd_shim::port<0x0400046Cu, 4u>())

#undef MATRIX_TRANSLATE
#define MATRIX_TRANSLATE (::twlhd_shim::port<0x04000470u, 4u>())

#undef MATRIX_RESTORE
#define MATRIX_RESTORE (::twlhd_shim::port<0x04000450u, 4u>())

#undef MATRIX_STORE
#define MATRIX_STORE (::twlhd_shim::port<0x0400044Cu, 4u>())

#undef MATRIX_IDENTITY
#define MATRIX_IDENTITY (::twlhd_shim::port<0x04000454u, 4u>())

#undef MATRIX_LOAD4x4
#define MATRIX_LOAD4x4 (::twlhd_shim::port<0x04000458u, 4u>())

#undef MATRIX_LOAD4x3
#define MATRIX_LOAD4x3 (::twlhd_shim::port<0x0400045Cu, 4u>())

#undef MATRIX_MULT4x4
#define MATRIX_MULT4x4 (::twlhd_shim::port<0x04000460u, 4u>())

#undef MATRIX_MULT4x3
#define MATRIX_MULT4x3 (::twlhd_shim::port<0x04000464u, 4u>())

#undef MATRIX_MULT3x3
#define MATRIX_MULT3x3 (::twlhd_shim::port<0x04000468u, 4u>())
