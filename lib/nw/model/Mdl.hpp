#pragma once

// Huge credit here to Torlack

#include "../log.hpp"
#include "../resources/ResourceData.hpp"
#include "../util/InternedString.hpp"
#include "../util/string.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace nw::model {

// -- Forward Decls
class Mdl;

// -- Constants ---------------------------------------------------------------

struct NodeFlags {
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

struct NodeType {
    static constexpr uint32_t camera = (NodeFlags::header | NodeFlags::camera); // Dunno about this one.
    static constexpr uint32_t dummy = NodeFlags::header;
    static constexpr uint32_t emitter = NodeFlags::header | NodeFlags::emitter;
    static constexpr uint32_t light = NodeFlags::header | NodeFlags::light;
    static constexpr uint32_t reference = NodeFlags::header | NodeFlags::reference;
    static constexpr uint32_t patch = NodeFlags::header | NodeFlags::patch;
    static constexpr uint32_t trimesh = NodeFlags::header | NodeFlags::mesh;
    static constexpr uint32_t danglymesh = trimesh | NodeFlags::dangly;
    static constexpr uint32_t skin = trimesh | NodeFlags::skin;
    static constexpr uint32_t animmesh = trimesh | NodeFlags::anim;
    static constexpr uint32_t aabb = trimesh | NodeFlags::aabb;

    static uint32_t from_string(std::string_view str)
    {
        if (string::icmp(str, "dummy")) return dummy;
        if (string::icmp(str, "reference")) return reference;
        if (string::icmp(str, "patch")) return patch;
        if (string::icmp(str, "trimesh")) return trimesh;
        if (string::icmp(str, "danglymesh")) return danglymesh;
        if (string::icmp(str, "skin")) return skin;
        if (string::icmp(str, "animmesh")) return animmesh;
        if (string::icmp(str, "emitter")) return emitter;
        if (string::icmp(str, "light")) return light;
        if (string::icmp(str, "aabb")) return aabb;
        if (string::icmp(str, "camera")) return camera;
        LOG_F(ERROR, "unkown node type: {}", str);
        return 0;
    }

    static constexpr std::string_view to_string(uint32_t value)
    {
        switch (value) {
        default:
            return "";
        case dummy:
            return "dummy";
        case reference:
            return "reference";
        case patch:
            return "patch";
        case trimesh:
            return "trimesh";
        case danglymesh:
            return "danglymesh";
        case skin:
            return "skin";
        case animmesh:
            return "animmesh";
        case emitter:
            return "emitter";
        case light:
            return "light";
        case aabb:
            return "aabb";
        case camera:
            return "camera";
        }
    }
};

struct EmitterFlag {
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

enum struct TriangleMode : uint32_t {
    triangle = 0x03,
    triangle_strip = 0x04,
};

enum struct GeometryFlag : uint32_t {
    geometry = 0x01,
    model = 0x02,
    animation = 0x04,
    binary = 0x80,
};

enum struct GeometryType : uint8_t {
    geometry = 1,
    model = 2,
    animation = 5,
};

enum struct ModelClass : uint32_t {
    invalid = 0,
    effect = 1,
    tile = 2,
    character = 4,
    door = 8,
    item = 16, // i dunno if these values have significance
    gui = 32,
};

inline std::string_view model_class_to_string(ModelClass cls)
{
    switch (cls) {
    default:
        return "";
    case ModelClass::invalid:
        return "UNKNOWN";
    case ModelClass::effect:
        return "EFFECT";
    case ModelClass::tile:
        return "TILE";
    case ModelClass::character:
        return "CHARACTER";
    case ModelClass::door:
        return "DOOR";
    case ModelClass::item:
        return "ITEM";
    case ModelClass::gui:
        return "GUI";
    }
}

struct ControllerType {

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
    static constexpr uint32_t LightningSubDiv = 220; // dunno
    static constexpr uint32_t Detonate = 228;
    static constexpr uint32_t AlphaMid = 464;
    static constexpr uint32_t ColorMid = 468;
    static constexpr uint32_t PercentStart = 480;
    static constexpr uint32_t PercentMid = 481;
    static constexpr uint32_t PercentEnd = 482;
    static constexpr uint32_t SizeMid = 484;
    static constexpr uint32_t SizeMid_Y = 488;
    static constexpr uint32_t lock_axes = 500;     // dunno, value is random
    static constexpr uint32_t spawn_type = 501;    // dunno, value is random
    static constexpr uint32_t random = 502;        // dunno, value is random
    static constexpr uint32_t inherit = 503;       // dunno, value is random
    static constexpr uint32_t inherit_local = 503; // dunno, value is random

    // Meshes

    static constexpr uint32_t SelfIllumColor = 100;
    static constexpr uint32_t Alpha = 128;

    static const std::unordered_map<std::string_view, std::pair<uint32_t, uint32_t>> map;
    static std::pair<uint32_t, uint32_t> lookup(std::string_view cont);
};

// -- Controller --------------------------------------------------------------

struct ControllerKey {
    ControllerKey(InternedString name_, uint32_t type_, int rows_, int key_offset_,
        int time_offset_, int data_offset_, int columns_, bool is_key_)
        : name{name_}
        , type{type_}
        , rows{rows_}
        , key_offset{key_offset_}
        , time_offset{time_offset_}
        , data_offset{data_offset_}
        , columns{columns_}
        , is_key{is_key_}
    {
    }

    InternedString name;
    uint32_t type;
    int rows{0};
    int key_offset{0};
    int time_offset{0};
    int data_offset{0};
    int columns{0};
    bool is_key{false};

    //
    // If bColumns == -1, then we have a keyed controller with
    //          NO VALUES AT ALL INCLUDING A KEY!!! (detonatekey)
    // If sRows == 1, then this is a normal controller
    // If sRows > 1 && bColumns <= 0x10, then this is a keyed controller
    // If sRows > 1 && bColumns > 0x10, then this is a bezier keyed controller
    //
};

struct ControllerValue {
    const ControllerKey* key = nullptr;
    std::span<const float> time;
    std::span<const float> data;
};

// -- Nodes -------------------------------------------------------------------

struct Face {
    std::array<uint32_t, 3> vert_idx;
    int32_t shader_group_idx;
    std::array<uint32_t, 3> tvert_idx;
    uint32_t material_idx;
};

struct Node {
    Node(std::string name_, uint32_t type_);
    virtual ~Node() = default;

    std::string name;
    const uint32_t type;
    bool inheritcolor = false;
    Node* parent = nullptr;
    std::vector<Node*> children;
    std::vector<ControllerKey> controller_keys;
    std::vector<float> controller_data;

    /// Adds a controller to a model node
    void add_controller_data(std::string_view name_, uint32_t type_, std::vector<float> times_,
        std::vector<float> data_, int rows_, int columns_ = 1);

    /// Gets a controller to a model node
    ControllerValue get_controller(uint32_t type_, bool key = false) const;
};

struct DummyNode : public Node {
    DummyNode(std::string name_);
};

struct CameraNode : public Node {
    CameraNode(std::string name_);
};

struct EmitterNode : public Node {
    EmitterNode(std::string name_);

    float blastlength{0.0f};
    float blastradius{0.0f};
    std::string blend;
    std::string chunkname;
    float deadspace{0.0f};
    uint32_t loop{0};
    std::string render;
    uint32_t renderorder{0};
    int32_t spawntype{0};
    std::string texture;
    uint32_t twosidedtex{0};
    std::string update;
    uint32_t xgrid{0};
    uint32_t ygrid{0};
    uint32_t flags{0};

    uint32_t render_sel{0};
    uint32_t blend_sel{0};
    uint32_t update_sel{0};
    uint32_t spawntype_sel{0};
    float opacity{0.0f};
    std::string p2p_type;
    uint32_t tilefade{0}; // This may be an accident on one model..
};

struct LightNode : public Node {
    LightNode(std::string name_);
    virtual ~LightNode() = default;

    int32_t lensflares{0}; // dunno, maybe obsolete?
    float flareradius{0.0f};
    float multiplier{0.0f};
    glm::vec3 color;
    std::vector<float> flaresizes;
    std::vector<float> flarepositions;
    std::vector<glm::vec3> flarecolorshifts;
    std::vector<std::string> textures;
    uint32_t lightpriority{5};
    int32_t ambientonly{0};
    bool dynamic{true};
    uint32_t affectdynamic{1};
    uint32_t shadow{1};
    uint32_t generateflare{0};
    uint32_t fadinglight{1};
};

struct PatchNode : public Node {
    PatchNode(std::string name_);
};

struct ReferenceNode : public Node {
    ReferenceNode(std::string name_);

    std::string refmodel;
    bool reattachable;
};

struct Vertex {
    glm::vec3 position{};
    glm::vec2 tex_coords{};
    glm::vec3 normal{};
    glm::vec4 tangent{};
};

struct TrimeshNode : public Node {
    TrimeshNode(std::string name_, uint32_t type_ = NodeType::trimesh);
    virtual ~TrimeshNode() = default;

    glm::vec3 ambient;
    bool beaming;
    glm::vec3 bmin;
    glm::vec3 bmax;
    std::string bitmap;
    glm::vec3 center;
    glm::vec3 diffuse;
    std::string materialname;
    bool render{true};
    std::string renderhint;
    bool rotatetexture{false};
    bool shadow{false};
    float shininess;
    glm::vec3 specular;
    std::array<std::string, 3> textures;
    uint32_t tilefade{0};
    int transparencyhint{0};
    bool showdispl{false}; // dunno
    uint32_t displtype{1}; // dunno
    uint32_t lightmapped{0};
    std::vector<std::string> multimaterial;
    std::vector<glm::vec3> colors;

    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
};

struct SkinVertex {
    glm::vec3 position{};
    glm::vec2 tex_coords{};
    glm::vec3 normal{};
    glm::vec4 tangent{};
    glm::i16vec4 bones{};
    glm::vec4 weights{};
};

struct SkinNode : public TrimeshNode {
    SkinNode(std::string name_);

    std::vector<SkinVertex> vertices;
    std::array<int16_t, 64> bone_nodes;
    std::vector<glm::vec3> bone_translation_inv;
    std::vector<glm::quat> bone_rotation_inv;
};

struct AnimeshNode : public TrimeshNode {
    AnimeshNode(std::string name_);

    std::vector<glm::vec3> animtverts;
    std::vector<glm::vec3> animverts;
    float sampleperiod;
    float cliph{0.0f}; // Dunno
    float clipw{0.0f}; // Dunno
    float clipv{0.0f}; // Dunno
    float clipu{0.0f}; // Dunno
};

struct DanglymeshNode : public TrimeshNode {
    DanglymeshNode(std::string name_);

    std::vector<float> constraints;
    float displacement;
    float period;
    float tightness;
};

struct AABBEntry {
    glm::vec3 bmin;
    glm::vec3 bmax;
    int32_t leaf_face;
    uint32_t plane; // 0x01 = Positive X
    // 0x02 = Positive Y
    // 0x04 = Positive Z
    // 0x08 = Negative X
    // 0x10 = Negative Y
    // 0x20 = Negative Z

    // std::unique_ptr<AABBEntry> left;
    // std::unique_ptr<AABBEntry> right;
};

struct AABBNode : public TrimeshNode {
    AABBNode(std::string name_);

    std::vector<AABBEntry> entries;
};

// -- Geometry ----------------------------------------------------------------

struct Geometry {
    Geometry(GeometryType type_ = GeometryType::geometry);
    Geometry(Geometry&) = delete;
    virtual ~Geometry() = default;

    Geometry& operator=(Geometry&) = delete;
    Node* find(const std::regex& re);
    const Node* find(const std::regex& re) const;

    std::string name;
    GeometryType type;
    std::vector<std::unique_ptr<Node>> nodes;
};

struct AnimationEvent {
    float time{0.0f};
    std::string name;
};

struct Animation : public Geometry {
    Animation(std::string name_);
    virtual ~Animation() = default;

    float length{1.0f};
    float transition_time{0.25f};
    std::string anim_root;
    std::vector<AnimationEvent> events;
};

struct Model : public Geometry {
    Model();
    Model(Model&) = delete;
    virtual ~Model() = default;

    Model& operator=(Model&) = delete;

    Animation* find_animation(std::string_view name);
    const Animation* find_animation(std::string_view name) const;

    ModelClass classification;
    bool ignorefog;
    std::vector<std::unique_ptr<Animation>> animations;
    // [TODO] Need to replace this with a mdl cache
    std::unique_ptr<Mdl> supermodel;
    glm::vec3 bmin;
    glm::vec3 bmax;
    float radius;
    float animationscale;
    std::string supermodel_name;
    std::string file_dependency;
};

/// Implements  Bioware MDL file format
/// @warning This is still incomplete
class Mdl {
    ResourceData data_;
    bool loaded_ = false;

public:
    Model model;

    Mdl(const std::filesystem::path& filename);
    Mdl(ResourceData data);

    std::unique_ptr<Node> make_node(uint32_t type, std::string_view name);
    bool valid() const;
};

// std::ostream& operator<<(std::ostream& out, const Mdl& model);

} // namespace nw::model
