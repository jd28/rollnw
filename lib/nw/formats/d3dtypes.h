/*==========================================================================;
 *
 *  Copyright (C) 1995-1998 Microsoft Corporation.  All Rights Reserved.
 *
 *  File:   d3dtypes.h
 *  Content:    Direct3D types include file
 *
 ***************************************************************************/

#ifndef _D3DTYPES_H_
#define _D3DTYPES_H_

#include <cstdint>

#ifndef RGB_MAKE

#ifndef D3DCOLOR_DEFINED
typedef uint32_t D3DCOLOR;
#define D3DCOLOR_DEFINED
#endif
typedef uint32_t* LPD3DCOLOR;

/*
 * Format of RGBA colors is
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |    alpha      |      red      |     green     |     blue      |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

inline constexpr uint8_t rgba_getalpha(uint32_t rgb)
{
    return static_cast<uint8_t>(rgb >> 24u);
}

inline constexpr uint8_t rgba_getred(uint32_t rgb)
{
    return static_cast<uint8_t>((rgb >> 16u) & 0xff);
}

inline constexpr uint8_t rgba_getgreen(uint32_t rgb)
{
    return static_cast<uint8_t>((rgb >> 8u) & 0xff);
}

inline constexpr uint8_t rgba_getblue(uint32_t rgb)
{
    return static_cast<uint8_t>(rgb & 0xff);
}

inline constexpr D3DCOLOR rgba_make(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return static_cast<D3DCOLOR>((a << 24u) | (r << 16u) | (g << 8u) | b);
}

/* D3DRGB and D3DRGBA may be used as initialisers for D3DCOLORs
 * The float values must be in the range 0..1
 */
#define D3DRGB(r, g, b) \
    (0xff000000L | ((static_cast<long>((r)*255)) << 16) | ((static_cast<long>((g)*255)) << 8) | static_cast<long>((b)*255))
#define D3DRGBA(r, g, b, a)                                                      \
    (((static_cast<long>((a)*255)) << 24) | ((static_cast<long>((r)*255)) << 16) \
        | ((static_cast<long>((g)*255)) << 8) | static_cast<long>((b)*255))

/*
 * Format of RGB colors is
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |    ignored    |      red      |     green     |     blue      |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
#define RGB_GETRED(rgb) (((rgb) >> 16) & 0xff)
#define RGB_GETGREEN(rgb) (((rgb) >> 8) & 0xff)
#define RGB_GETBLUE(rgb) ((rgb)&0xff)

inline constexpr uint32_t rgba_setalpha(uint32_t rgba, uint8_t a)
{
    uint32_t a2 = a;
    return (a2 << 24u) | (rgba & 0x00ffffffu);
}

#define RGB_MAKE(r, g, b) ((D3DCOLOR)(((r) << 16) | ((g) << 8) | (b)))
#define RGBA_TORGB(rgba) ((D3DCOLOR)((rgba)&0xffffff))
#define RGB_TORGBA(rgb) ((D3DCOLOR)((rgb) | 0xff000000))

#endif

#endif /* _D3DTYPES_H_ */
