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

/*
 * Format of RGBA colors is
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |    alpha      |      red      |     green     |     blue      |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
#define RGBA_GETALPHA(rgb) ((rgb) >> 24)
#define RGBA_GETRED(rgb) (((rgb) >> 16) & 0xff)
#define RGBA_GETGREEN(rgb) (((rgb) >> 8) & 0xff)
#define RGBA_GETBLUE(rgb) ((rgb)&0xff)
#define RGBA_MAKE(r, g, b, a) ((D3DCOLOR)(((a) << 24) | ((r) << 16) | ((g) << 8) | (b)))

/* D3DRGB and D3DRGBA may be used as initialisers for D3DCOLORs
 * The float values must be in the range 0..1
 */
#define D3DRGB(r, g, b) \
    (0xff000000L | (((long)((r)*255)) << 16) | (((long)((g)*255)) << 8) | (long)((b)*255))
#define D3DRGBA(r, g, b, a)                                \
    ((((long)((a)*255)) << 24) | (((long)((r)*255)) << 16) \
        | (((long)((g)*255)) << 8) | (long)((b)*255))

/*
 * Format of RGB colors is
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |    ignored    |      red      |     green     |     blue      |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
#define RGB_GETRED(rgb) (((rgb) >> 16) & 0xff)
#define RGB_GETGREEN(rgb) (((rgb) >> 8) & 0xff)
#define RGB_GETBLUE(rgb) ((rgb)&0xff)
#define RGBA_SETALPHA(rgba, x) (((x) << 24) | ((rgba)&0x00ffffff))
#define RGB_MAKE(r, g, b) ((D3DCOLOR)(((r) << 16) | ((g) << 8) | (b)))
#define RGBA_TORGB(rgba) ((D3DCOLOR)((rgba)&0xffffff))
#define RGB_TORGBA(rgb) ((D3DCOLOR)((rgb) | 0xff000000))

#endif

#ifndef D3DCOLOR_DEFINED
typedef uint32_t D3DCOLOR;
#define D3DCOLOR_DEFINED
#endif
typedef uint32_t* LPD3DCOLOR;

#endif /* _D3DTYPES_H_ */
