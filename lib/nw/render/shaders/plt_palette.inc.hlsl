static const uint PLT_LAYER_COUNT = 10u;
static const uint PLT_PALETTE_MAX_WIDTH = 256u;
static const uint PLT_PALETTE_MAX_HEIGHT = 176u;
static const uint PLT_PALETTE_PIXEL_COUNT = PLT_PALETTE_MAX_WIDTH * PLT_PALETTE_MAX_HEIGHT;

struct PltPaletteLayer {
    uint4 dimensions;
    uint colors[PLT_PALETTE_PIXEL_COUNT];
};

StructuredBuffer<PltPaletteLayer> g_plt_palette : register(t2, space0);

uint plt_palette_width(uint layer) {
    return g_plt_palette[layer].dimensions.x;
}

uint plt_palette_height(uint layer) {
    return g_plt_palette[layer].dimensions.y;
}

uint plt_palette_color(uint layer, uint row, uint color) {
    return g_plt_palette[layer].colors[row * PLT_PALETTE_MAX_WIDTH + color];
}
