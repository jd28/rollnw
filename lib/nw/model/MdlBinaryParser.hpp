#pragma once

#include "../util/macros.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace nw::model {

namespace detail {

#define STATIC_ASSERT_SIZE(type) \
    static_assert(sizeof(type) == type::s_sizeof, ROLLNW_STRINGIFY(type) " is the incorrect size")

struct MdlBinaryHeader {
    static constexpr uint32_t s_sizeof = 0x000C;

    uint32_t type;
    uint32_t raw_data_offset;
    uint32_t raw_data_size;
};
STATIC_ASSERT_SIZE(MdlBinaryHeader);

struct MdlBinaryArray {
    static constexpr uint32_t s_sizeof = 0x000C;

    uint32_t offset;
    uint32_t length;
    uint32_t allocated;
};
STATIC_ASSERT_SIZE(MdlBinaryArray);

struct MdlBinaryController {
    static constexpr uint32_t s_sizeof = 0x000C;

    int32_t type;
    int16_t rows;
    int16_t time_index;
    int16_t data_index;
    int8_t columns; ///< without time
    int8_t pad0;
};
STATIC_ASSERT_SIZE(MdlBinaryController);

struct MdlBinaryGeometryHeader {
    static constexpr uint32_t s_sizeof = 0x0070;

    uint32_t pad0[2];
    std::array<char, 64> name;
    uint32_t root_node_offset;
    uint32_t node_count;
    MdlBinaryArray pad1[2];
    uint32_t refcount; ///< ignore
    uint8_t geometry_type;
    uint8_t pad2[3];
};
STATIC_ASSERT_SIZE(MdlBinaryGeometryHeader);

struct MdlBinaryModelHeader {
    static constexpr uint32_t s_sizeof = 0x00E8;

    MdlBinaryGeometryHeader geometry_header;
    uint8_t pad0[2];
    uint8_t classification;
    uint8_t ignorefog;
    uint32_t pad1;
    MdlBinaryArray animations;
    uint32_t parent; // ignore
    glm::vec3 bbmin;
    glm::vec3 bbmax;
    float radius;
    float animation_scale;
    std::array<char, 64> supermodel;
};
STATIC_ASSERT_SIZE(MdlBinaryModelHeader);

struct MdlBinaryAnimationHeader {
    static constexpr uint32_t s_sizeof = 0x00C4;

    MdlBinaryGeometryHeader geometry_header;
    float length;
    float transition_time;
    std::array<char, 64> root;
    MdlBinaryArray events;
};
STATIC_ASSERT_SIZE(MdlBinaryAnimationHeader);

struct MdlBinaryAnimationEvent {
    static constexpr uint32_t s_sizeof = 0x0024;
    float after;
    std::array<char, 32> name;
};
STATIC_ASSERT_SIZE(MdlBinaryAnimationEvent);

struct MdlBinaryFace {
    static constexpr uint32_t s_sizeof = 0x0020;

    glm::vec3 normal;
    float distance;
    int32_t surface_id;
    std::array<int16_t, 3> adjacent_faces;
    std::array<int16_t, 3> vertex_indicies;
};
STATIC_ASSERT_SIZE(MdlBinaryFace);

struct MdlBinaryNodeHeader {
    static constexpr uint32_t s_sizeof = 0x0070;

    uint32_t pad0[6];
    uint32_t inherit_color;
    uint32_t nth_node;
    std::array<char, 32> name;
    uint32_t geometry_ptr;          ///< ignore
    uint32_t parent_ptr;            ///< ignore
    MdlBinaryArray children;        ///< pointer array
    MdlBinaryArray controller_keys; ///< MdlBinaryController?
    MdlBinaryArray controller_data; ///< Float
    uint32_t type;
};
STATIC_ASSERT_SIZE(MdlBinaryNodeHeader);

struct MdlBinaryMeshHeader {
    static constexpr uint32_t s_sizeof = 0x0270;

    MdlBinaryNodeHeader node_header;
    uint32_t pad0[2];
    MdlBinaryArray faces; ///< MdlBinaryFace
    glm::vec3 bbmin;
    glm::vec3 bbmax;
    float radius;
    glm::vec3 point_average;
    glm::vec3 diffuse;
    glm::vec3 ambient;
    glm::vec3 specular;
    float shininess;
    uint32_t shadow;
    uint32_t beaming;
    uint32_t render_flag;
    uint32_t transparency_hint;
    uint32_t render_hint; ///< NWN:EE
    std::array<char, 64> texture0;
    std::array<char, 64> texture1;
    std::array<char, 64> texture2;
    std::array<char, 64> texture3; ///< This is material name in NWN:EE
    uint32_t tile_fade;
    MdlBinaryArray vertex_indicies; ///< Unstored
    MdlBinaryArray left_over_faces;
    MdlBinaryArray vertex_indices_count;
    MdlBinaryArray vertex_indices_offset;
    uint32_t pad1[2];
    uint8_t triangle_mode;
    uint8_t pad2[3];
    uint32_t pad3;
    uint32_t vertices; ///< Raw vec3 Data
    uint16_t vertex_count;
    uint16_t texture_count;
    uint32_t texture0_vert_ptr;                    ///< Raw vec2 Data
    uint32_t texture1_vert_ptr;                    ///< Raw vec2 Data
    uint32_t texture2_vert_ptr;                    ///< Raw vec2 Data
    uint32_t texture3_vert_ptr;                    ///< Raw vec2 Data
    uint32_t vertex_normals_ptr;                   ///< Raw vec3 Data
    uint32_t vertex_color_ptr;                     ///< Raw RGBA Data
    std::array<uint32_t, 5> texture_anim_data_ptr; ///< Raw vec3 data
    uint32_t texture_anim_data_ptr2;               ///< Raw float? data
    uint8_t lightmapped;
    uint8_t rotate_texture;
    uint16_t pad4;
    float vertex_normal_sum;
    uint32_t pad5;
};
STATIC_ASSERT_SIZE(MdlBinaryMeshHeader);

struct MdlBinaryDummyNode {
    static constexpr uint32_t s_sizeof = 0x0070;

    MdlBinaryNodeHeader header;
};
STATIC_ASSERT_SIZE(MdlBinaryDummyNode);

struct MdlBinaryLightNode {
    static constexpr uint32_t s_sizeof = 0x00CC;

    MdlBinaryNodeHeader header;
    float flare_radius;
    MdlBinaryArray pad0;
    MdlBinaryArray flare_sizes;        ///< Float
    MdlBinaryArray flare_positions;    ///< Float
    MdlBinaryArray flare_color_shifts; ///< vec3
    MdlBinaryArray textures;           ///< char*
    uint32_t priority;
    uint32_t ambient_only;
    uint32_t dynamic_type;
    uint32_t affect_dynamic;
    uint32_t shadow;
    uint32_t generate_flare;
    uint32_t fading;
};
STATIC_ASSERT_SIZE(MdlBinaryLightNode);

struct MdlBinaryEmitterNode {
    static constexpr uint32_t s_sizeof = 0x0148;

    MdlBinaryNodeHeader header;
    float dead_space;
    float blast_radius;
    float blast_lenght;
    uint32_t x_grid;
    uint32_t y_grid;
    uint32_t space_type;
    std::array<char, 32> update;
    std::array<char, 32> render;
    std::array<char, 32> blend;
    std::array<char, 64> texture;
    std::array<char, 16> chunk_name;
    uint32_t twosided_texture;
    uint32_t loop;
    uint16_t render_order;
    uint16_t pad0;
    uint32_t flags;
};
STATIC_ASSERT_SIZE(MdlBinaryEmitterNode);

struct MdlBinaryReferenceNode {
    static constexpr uint32_t s_sizeof = 0x00B4;

    MdlBinaryNodeHeader header;
    std::array<char, 64> ref;
    uint32_t reattachable;
};
STATIC_ASSERT_SIZE(MdlBinaryReferenceNode);

struct MdlBinaryTrimeshNode {
    static constexpr uint32_t s_sizeof = 0x0270;

    MdlBinaryMeshHeader header;
};
STATIC_ASSERT_SIZE(MdlBinaryTrimeshNode);

struct MdlBinarySkinNode {
    static constexpr uint32_t s_sizeof = 0x02D4;

    MdlBinaryMeshHeader header;
    MdlBinaryArray weight_array;   // Ignore
    uint32_t weights_ptr;          ///< Raw vec4 data
    uint32_t bones_ptr;            ///< Raw uint16[4] data
    uint32_t bone_ref_mapping_ptr; ///< Raw uint16 data
    uint32_t mapping_count;
    MdlBinaryArray qbone_ref_inv;         ///< quaternion
    MdlBinaryArray tbone_ref_inv;         ///< vec3
    MdlBinaryArray bone_constant_indices; ///< uint32
    std::array<uint16_t, 17> bone_to_nodes;
    uint16_t pad0;
};
STATIC_ASSERT_SIZE(MdlBinarySkinNode);

// GUI
struct MdlBinaryAnimmeshNode {
    static constexpr uint32_t s_sizeof = 0x02A8;

    MdlBinaryMeshHeader header;
    float sample_period;
    MdlBinaryArray animation_verts;    ///< Unstored
    MdlBinaryArray animation_tverts;   ///< Unstored
    MdlBinaryArray animation_normals;  ///< Unstored
    uint32_t animation_vert_info_ptr;  ///< vec3
    uint32_t animation_tvert_info_ptr; ///< vec2
    uint32_t vert_set_count;
    uint32_t tvert_set_count;
};
STATIC_ASSERT_SIZE(MdlBinaryAnimmeshNode);

struct MdlBinaryDanglymeshNode {
    static constexpr uint32_t s_sizeof = 0x0288;

    MdlBinaryMeshHeader header;
    MdlBinaryArray vert_constraints; ///< Float
    float displacement;
    float tightness;
    float period;
};
STATIC_ASSERT_SIZE(MdlBinaryDanglymeshNode);

struct MdlBinaryAABBEntry {
    static constexpr uint32_t s_sizeof = 0x0028;

    glm::vec3 bbmin;
    glm::vec3 bbmax;
    uint32_t left_ptr;  ///< MdlBinaryAABBEntry
    uint32_t right_ptr; ///< MdlBinaryAABBEntry
    int32_t leaf_face;
    uint32_t most_significant_plane;
};
STATIC_ASSERT_SIZE(MdlBinaryAABBEntry);

struct MdlBinaryAABBNode {
    static constexpr uint32_t s_sizeof = 0x0274;

    MdlBinaryMeshHeader header;
    uint32_t aabb_root_ptr; ///< MdlBinaryAABBEntry
};
STATIC_ASSERT_SIZE(MdlBinaryAABBNode);

#undef STATIC_ASSERT_SIZE

} // namespace detail

class Mdl;
struct Geometry;
struct Node;

class BinaryParser {
    Mdl* mdl_;
    std::span<uint8_t> bytes_;
    detail::MdlBinaryHeader header;

    bool parse_anim(const detail::MdlBinaryAnimationHeader& data);
    bool parse_geometry(Geometry* geometry, const detail::MdlBinaryGeometryHeader& data);
    bool parse_model(uint32_t offset);
    bool parse_node(uint32_t offset, Geometry* geometry, Node* parent = nullptr);

public:
    BinaryParser(std::span<uint8_t> bytes, Mdl* mdl);
    bool parse();
};

} // namespace nw::model
