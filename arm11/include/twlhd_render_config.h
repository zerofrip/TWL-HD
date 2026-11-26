// SPDX-License-Identifier: Apache-2.0
/*
 * Renderer-wide render configuration.
 *
 * This header must be freestanding: it is included from arm11/source/renderer.c
 * (where 3ds headers are available) and from host structural tests (where they
 * are not).  Do not add libctru/libnds includes here.
 */
#pragma once

#include <stdint.h>

/*
 * Default clear colour for the top screen render target.
 *
 * EQ-4z (docs/reports/eq4z-clear-black.md) changed this from the old
 * dark blue-grey 0x101018FF = (16,16,24) to opaque black 0x000000FF = (0,0,0)
 * so that the arm11 background matches the real DS top-screen background
 * captured on hardware (docs/reports/eq4y-gate-hw-result.md §6/§10(b)).
 *
 * The macro is intentionally overridable at compile time so diagnostic builds
 * can still reproduce the old colour without editing source.  Use the
 * append-style hook in arm11/Makefile (do NOT set CFLAGS=... directly):
 *
 *   make -C arm11 -B TWLHD_EXTRA_CFLAGS=-DTWLHD_RENDER_CLEAR_COLOR=0x101018FFu
 *
 * The alpha channel must remain 0xFF; a non-opaque clear would change the
 * effective background of the opaque draw paths.
 */
#ifndef TWLHD_RENDER_CLEAR_COLOR
#define TWLHD_RENDER_CLEAR_COLOR 0x000000FFu
#endif

/* Convenience: RGBA8 components of TWLHD_RENDER_CLEAR_COLOR. */
#define TWLHD_RENDER_CLEAR_R ((TWLHD_RENDER_CLEAR_COLOR >> 24) & 0xFFu)
#define TWLHD_RENDER_CLEAR_G ((TWLHD_RENDER_CLEAR_COLOR >> 16) & 0xFFu)
#define TWLHD_RENDER_CLEAR_B ((TWLHD_RENDER_CLEAR_COLOR >>  8) & 0xFFu)
#define TWLHD_RENDER_CLEAR_A ((TWLHD_RENDER_CLEAR_COLOR      ) & 0xFFu)
