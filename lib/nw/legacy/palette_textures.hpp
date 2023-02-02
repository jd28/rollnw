#pragma once

#include <cstddef>
#include <cstdint>

extern const uint8_t pal_armor01_tga[];
extern const size_t pal_armor01_tga_len;
extern const uint8_t pal_armor02_tga[];
extern const size_t pal_armor02_tga_len;
extern const uint8_t pal_cloth01_tga[];
extern const size_t pal_cloth01_tga_len;
extern const uint8_t pal_hair01_tga[];
extern const size_t pal_hair01_tga_len;
extern const uint8_t pal_leath01_tga[];
extern const size_t pal_leath01_tga_len;
extern const uint8_t pal_skin01_tga[];
extern const size_t pal_skin01_tga_len;
extern const uint8_t pal_tattoo01_tga[];
extern const size_t pal_tattoo01_tga_len;

struct TexturePalette {
    const uint8_t* texture;
    size_t size;
};

static const TexturePalette texture_palettes[10]{
    {pal_skin01_tga, pal_skin01_tga_len},     // skin
    {pal_hair01_tga, pal_hair01_tga_len},     // hair
    {pal_armor01_tga, pal_armor01_tga_len},   // metal1
    {pal_armor02_tga, pal_armor02_tga_len},   // metal2
    {pal_cloth01_tga, pal_cloth01_tga_len},   // cloth1
    {pal_cloth01_tga, pal_cloth01_tga_len},   // cloth2
    {pal_leath01_tga, pal_leath01_tga_len},   // leather1
    {pal_leath01_tga, pal_leath01_tga_len},   // leather2
    {pal_tattoo01_tga, pal_tattoo01_tga_len}, // tattoo1
    {pal_tattoo01_tga, pal_tattoo01_tga_len}, // tattoo2
};
