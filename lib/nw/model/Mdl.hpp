#pragma once

// Huge credit here to Torlack

#include "../util/ByteArray.hpp"
#include "../util/string.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <vector>

namespace nw {

// -- Constants ---------------------------------------------------------------

struct MdlNodeFlags {
    static constexpr uint32_t header = 0x00000001;
    static constexpr uint32_t light = 0x00000002;
    static constexpr uint32_t emitter = 0x00000004;
    static constexpr uint32_t camera = 0x00000008;
    static constexpr uint32_t reference = 0x00000010;
    static constexpr uint32_t mesh = 0x00000020;
    static constexpr uint32_t skin = 0x00000040;
    static constexpr uint32_t anim = 0x00000080;
    static constexpr uint32_t dangly = 0x00000100;
    static constexpr uint32_t aabb = 0x00000200;
    static constexpr uint32_t patch = 0x00000400;
};

struct MdlNodeType {
    static constexpr uint32_t camera = (MdlNodeFlags::header | MdlNodeFlags::camera); // Dunno about this one.
    static constexpr uint32_t dummy = MdlNodeFlags::header;
    static constexpr uint32_t emitter = MdlNodeFlags::header | MdlNodeFlags::emitter;
    static constexpr uint32_t light = MdlNodeFlags::header | MdlNodeFlags::light;
    static constexpr uint32_t reference = MdlNodeFlags::header | MdlNodeFlags::reference;
    static constexpr uint32_t patch = MdlNodeFlags::header | MdlNodeFlags::patch;
    static constexpr uint32_t trimesh = MdlNodeFlags::header | MdlNodeFlags::mesh;
    static constexpr uint32_t danglymesh = trimesh | MdlNodeFlags::dangly;
    static constexpr uint32_t skin = trimesh | MdlNodeFlags::skin;
    static constexpr uint32_t animmesh = trimesh | MdlNodeFlags::anim;
    static constexpr uint32_t aabb = trimesh | MdlNodeFlags::aabb;

    static uint32_t from_string(std::string_view str)
    {
        if (string::icmp(str, "Dummy")) return dummy;
        if (string::icmp(str, "Reference")) return reference;
        if (string::icmp(str, "Patch")) return patch;
        if (string::icmp(str, "Trimesh")) return trimesh;
        if (string::icmp(str, "Danglymesh")) return danglymesh;
        if (string::icmp(str, "Skin")) return skin;
        if (string::icmp(str, "Animmesh")) return animmesh;
        if (string::icmp(str, "Emitter")) return emitter;
        if (string::icmp(str, "Light")) return light;
        if (string::icmp(str, "AABB")) return aabb;
        if (string::icmp(str, "Camera")) return camera;
        return 0;
    }
};

struct MdlEmitterFlag {
    static constexpr uint32_t P2P = 0x0001;
    static constexpr uint32_t P2PSel = 0x0002;
    static constexpr uint32_t AffectedByWind = 0x0004;
    static constexpr uint32_t IsTinted = 0x0008;
    static constexpr uint32_t Bounce = 0x0010;
    static constexpr uint32_t Random = 0x0020;
    static constexpr uint32_t Inherit = 0x0040;
    static constexpr uint32_t InheritVel = 0x0080;
    static constexpr uint32_t InheritLocal = 0x0100;
    static constexpr uint32_t Splat = 0x0200;
    static constexpr uint32_t InheritPart = 0x0400;
};

enum struct MdlTriangleMode : uint32_t {
    triangle = 0x03,
    triangle_strip = 0x04,
};

enum struct MdlGeometryFlag : uint32_t {
    HasGeometry = 0x01,
    HasModel = 0x02,
    animation = 0x04,
    LoadedBinary = 0x80,
};

enum struct MdlGeometryType : uint8_t {
    geometry = 1,
    model = 2,
    animation = 5,
};

enum struct MdlModelClass : uint32_t {
    invalid = 0,
    effect = 1,
    tile = 2,
    character = 4,
    door = 8,
};

struct MdlControllerType {

    // Common to all nodes

    static constexpr uint32_t Position = 8;
    static constexpr uint32_t Orientation = 20;
    static constexpr uint32_t Scale = 36;
    static constexpr uint32_t Wirecolor = 20004;

    // Light

    static constexpr uint32_t Color = 76;
    static constexpr uint32_t Radius = 88;
    static constexpr uint32_t ShadowRadius = 96;
    static constexpr uint32_t VerticalDisplacement = 100;
    static constexpr uint32_t Multiplier = 140;

    // Emitter

    static constexpr uint32_t AlphaEnd = 80;
    static constexpr uint32_t AlphaStart = 84;
    static constexpr uint32_t BirthRate = 88;
    static constexpr uint32_t Bounce_Co = 92;
    static constexpr uint32_t ColorEnd = 96;
    static constexpr uint32_t ColorStart = 108;
    static constexpr uint32_t CombineTime = 120;
    static constexpr uint32_t Drag = 124;
    static constexpr uint32_t FPS = 128;
    static constexpr uint32_t FrameEnd = 132;
    static constexpr uint32_t FrameStart = 136;
    static constexpr uint32_t Grav = 140;
    static constexpr uint32_t LifeExp = 144;
    static constexpr uint32_t Mass = 148;
    static constexpr uint32_t P2P_Bezier2 = 152;
    static constexpr uint32_t P2P_Bezier3 = 156;
    static constexpr uint32_t ParticleRot = 160;
    static constexpr uint32_t RandVel = 164;
    static constexpr uint32_t SizeStart = 168;
    static constexpr uint32_t SizeEnd = 172;
    static constexpr uint32_t SizeStart_Y = 176;
    static constexpr uint32_t SizeEnd_Y = 180;
    static constexpr uint32_t Spread = 184;
    static constexpr uint32_t Threshold = 188;
    static constexpr uint32_t Velocity = 192;
    static constexpr uint32_t XSize = 196;
    static constexpr uint32_t YSize = 200;
    static constexpr uint32_t BlurLength = 204;
    static constexpr uint32_t LightningDelay = 208;
    static constexpr uint32_t LightningRadius = 212;
    static constexpr uint32_t LightningScale = 216;
    static constexpr uint32_t Detonate = 228;
    static constexpr uint32_t AlphaMid = 464;
    static constexpr uint32_t ColorMid = 468;
    static constexpr uint32_t PercentStart = 480;
    static constexpr uint32_t PercentMid = 481;
    static constexpr uint32_t PercentEnd = 482;
    static constexpr uint32_t SizeMid = 484;
    static constexpr uint32_t SizeMid_Y = 488;

    // Meshes

    static constexpr uint32_t SelfIllumColor = 100;
    static constexpr uint32_t Alpha = 128;
};

// -- Controller --------------------------------------------------------------

struct MdlControllerKey {
    MdlControllerKey(uint32_t type_, int rows_, int key_offset_, int data_offset_, int columns_)
        : type{type_}
        , rows{rows_}
        , key_offset{key_offset_}
        , data_offset{data_offset_}
        , columns{columns_}
    {
    }

    uint32_t type;
    int rows;
    int key_offset;
    int data_offset;
    int columns;

    //
    // If bColumns == -1, then we have a keyed controller with
    //          NO VALUES AT ALL INCLUDING A KEY!!! (detonatekey)
    // If sRows == 1, then this is a normal controller
    // If sRows > 1 && bColumns <= 0x10, then this is a keyed controller
    // If sRows > 1 && bColumns > 0x10, then this is a bezier keyed controller
    //
};

// -- Nodes -------------------------------------------------------------------

struct MdlFace {
    uint32_t vert_idx[3];
    uint32_t shader_group_idx;
    uint32_t tvert_idx[3];
    uint32_t material_idx;
};

struct MdlNode {
    MdlNode(std::string name, uint32_t type);
    virtual ~MdlNode() = default;

    std::string name;
    const uint32_t type;
    bool inheritcolor = false;
    glm::vec3 position;
    MdlNode* parent = nullptr;
    std::vector<MdlNode*> children;
    std::vector<MdlControllerKey> controller_keys;
    std::vector<float> controller_data;

    void add_controller_data(uint32_t type_, std::vector<float> data_, int rows_, int columns_ = 1)
    {
        MdlControllerKey k(type_, rows_, static_cast<int>(controller_keys.size()),
            static_cast<int>(controller_data.size()), columns_);
        controller_keys.push_back(k);
        for (float f : data_) {
            controller_data.push_back(f);
        }
    }
};

struct MdlDummyNode : public MdlNode {
    MdlDummyNode(std::string name);
};

struct MdlCameraNode : public MdlNode {
    MdlCameraNode(std::string name);
};

struct MdlEmitterNode : public MdlNode {
    MdlEmitterNode(std::string name);

    float blastlength{0.0f};
    float blastradius{0.0f};
    std::string blend;
    std::string chunkname;
    float deadspace{0.0f};
    uint32_t loop{0};
    std::string render;
    uint32_t renderorder{0};
    uint32_t spawntype{0};
    std::string texture;
    uint32_t twosidedtex{0};
    std::string update;
    uint32_t xgrid{0};
    uint32_t ygrid{0};
    uint32_t flags{0};
};

struct MdlLightNode : public MdlNode {
    MdlLightNode(std::string name);
    virtual ~MdlLightNode() = default;

    float flareradius{0.0f};
    std::vector<float> flaresizes;
    std::vector<float> flarepositions;
    std::vector<glm::vec3> flarecolorshifts;
    std::vector<std::string> textures;
    uint32_t lightpriority{5};
    uint32_t ambientonly{0};
    bool dynamic{true};
    uint32_t affectdynamic{1};
    uint32_t shadow{1};
    uint32_t generateflare{0};
    uint32_t fadinglight{1};
};

struct MdlPatchNode : public MdlNode {
    MdlPatchNode(std::string name);
};

struct MdlReferenceNode : public MdlNode {
    MdlReferenceNode(std::string name);

    std::string refmodel;
    bool reattachable;
};

struct MdlTrimeshNode : public MdlNode {
    MdlTrimeshNode(std::string name, uint32_t type = MdlNodeType::trimesh);
    virtual ~MdlTrimeshNode() = default;

    glm::vec3 ambient;
    bool beaming;
    glm::vec3 bmin;
    glm::vec3 bmax;
    std::string bitmap;
    std::vector<uint32_t> colors;
    glm::vec3 center;
    glm::vec3 diffuse;
    std::vector<MdlFace> faces;
    std::string materialname;
    bool render;
    uint32_t renderhint;
    bool rotatetexture;
    bool shadow;
    float shininess;
    glm::vec3 specular;
    std::string textures[3];
    uint32_t tilefade;
    bool transparencyhint;
    std::vector<glm::vec3> verts;
    std::vector<glm::vec3> tverts[4];
};

struct MdlSkinWeight {
    char bones[4][32];
    float weights[4];
};

struct MdlSkinNode : public MdlTrimeshNode {
    MdlSkinNode(std::string name);

    std::vector<MdlSkinWeight> weights;
    std::vector<glm::quat> qbone_ref_inv;
    std::vector<glm::vec3> tbone_ref_inv;
    std::vector<uint32_t> boneconstantindices;
};

struct MdlAnimeshNode : public MdlTrimeshNode {
    MdlAnimeshNode(std::string name);

    std::vector<glm::vec3> animtverts;
    std::vector<glm::vec3> animverts;
    float sampleperiod;
};

struct MdlDanglymeshNode : public MdlTrimeshNode {
    MdlDanglymeshNode(std::string name);

    std::vector<float> constraints;
    float displacement;
    float period;
    float tightness;
};

struct MdlAABBEntry {
    glm::vec3 bmin;
    glm::vec3 bmax;
    uint32_t leaf_face;
    uint32_t plane; // 0x01 = Positive X
    // 0x02 = Positive Y
    // 0x04 = Positive Z
    // 0x08 = Negative X
    // 0x10 = Negative Y
    // 0x20 = Negative Z

    // std::unique_ptr<MdlAABBEntry> left;
    // std::unique_ptr<MdlAABBEntry> right;
};

struct MdlAABBNode : public MdlTrimeshNode {
    MdlAABBNode(std::string name);

    std::vector<MdlAABBEntry> entries;
};

// -- Geometry ----------------------------------------------------------------

struct MdlGeometry {
    MdlGeometry(MdlGeometryType type = MdlGeometryType::geometry);
    virtual ~MdlGeometry() = default;

    std::string name;
    MdlGeometryType type;
    std::vector<std::unique_ptr<MdlNode>> nodes;
};

struct MdlAnimationEvent {
    float afer;
    char name[32];
};

struct MdlAnimation : public MdlGeometry {
    MdlAnimation(std::string name);
    virtual ~MdlAnimation() = default;

    float length_;
    float transition_time_;
    char anim_root_[64];
    std::vector<MdlAnimationEvent> events_;
};

struct MdlModel : public MdlGeometry {
    MdlModel();
    virtual ~MdlModel() = default;

    MdlModelClass classification;
    bool ignorefog;
    std::vector<MdlAnimation*> animations;
    std::unique_ptr<MdlModel> supermodel;
    glm::vec3 bmin;
    glm::vec3 bmax;
    float radius;
    float animationscale;
    std::string supermodel_name;
};

class Mdl {
    ByteArray bytes_;
    bool loaded_ = false;

    bool parse_binary();

public:
    MdlModel model;

    enum EnumFlags {
        EnumFlag_Geometry = 0x00000001,
        EnumFlag_Animation = 0x00000002,
    };

    Mdl(const std::string& filename);

    MdlNode* add_node(uint32_t type, std::string_view name);
    bool valid() const;
};

} // namespace nw
