#include "casters.hpp"
#include "opaque_types.hpp"

#include <nw/model/Mdl.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/platform.hpp>

#include <nanobind/nanobind.h>
#include <nanobind/stl/filesystem.h>

#include <filesystem>
#include <string>
#include <utility>
#include <variant>

namespace py = nanobind;

void init_model(py::module_& nw)
{
    py::class_<nw::model::NodeFlags>(nw, "MdlNodeFlags")
        .def_ro_static("header", &nw::model::NodeFlags::header)
        .def_ro_static("light", &nw::model::NodeFlags::light)
        .def_ro_static("emitter", &nw::model::NodeFlags::emitter)
        .def_ro_static("camera", &nw::model::NodeFlags::camera)
        .def_ro_static("reference", &nw::model::NodeFlags::reference)
        .def_ro_static("mesh", &nw::model::NodeFlags::mesh)
        .def_ro_static("skin", &nw::model::NodeFlags::skin)
        .def_ro_static("anim", &nw::model::NodeFlags::anim)
        .def_ro_static("dangly", &nw::model::NodeFlags::dangly)
        .def_ro_static("aabb", &nw::model::NodeFlags::aabb)
        .def_ro_static("patch", &nw::model::NodeFlags::patch);

    py::class_<nw::model::NodeType>(nw, "MdlNodeType")
        .def_ro_static("camera", &nw::model::NodeType::camera)
        .def_ro_static("dummy", &nw::model::NodeType::dummy)
        .def_ro_static("emitter", &nw::model::NodeType::emitter)
        .def_ro_static("light", &nw::model::NodeType::light)
        .def_ro_static("reference", &nw::model::NodeType::reference)
        .def_ro_static("patch", &nw::model::NodeType::patch)
        .def_ro_static("trimesh", &nw::model::NodeType::trimesh)
        .def_ro_static("danglymesh", &nw::model::NodeType::danglymesh)
        .def_ro_static("skin", &nw::model::NodeType::skin)
        .def_ro_static("animmesh", &nw::model::NodeType::animmesh)
        .def_ro_static("aabb", &nw::model::NodeType::aabb);

    py::class_<nw::model::EmitterFlag>(nw, "MdlEmitterFlag")
        .def_ro_static("affected_by_wind", &nw::model::EmitterFlag::AffectedByWind)
        .def_ro_static("bounce", &nw::model::EmitterFlag::Bounce)
        .def_ro_static("inherit_local", &nw::model::EmitterFlag::InheritLocal)
        .def_ro_static("inherit_part", &nw::model::EmitterFlag::InheritPart)
        .def_ro_static("inherit_vel", &nw::model::EmitterFlag::InheritVel)
        .def_ro_static("inherit", &nw::model::EmitterFlag::Inherit)
        .def_ro_static("is_tinted", &nw::model::EmitterFlag::IsTinted)
        .def_ro_static("p2p_sel", &nw::model::EmitterFlag::P2PSel)
        .def_ro_static("p2p", &nw::model::EmitterFlag::P2P)
        .def_ro_static("random", &nw::model::EmitterFlag::Random)
        .def_ro_static("splat", &nw::model::EmitterFlag::Splat);

    py::enum_<nw::model::TriangleMode>(nw, "MdlTriangleMode")
        .value("triangle", nw::model::TriangleMode::triangle)
        .value("triangle_strip", nw::model::TriangleMode::triangle_strip);

    py::enum_<nw::model::GeometryFlag>(nw, "MdlGeometryFlag")
        .value("geometry", nw::model::GeometryFlag::geometry)
        .value("model", nw::model::GeometryFlag::model)
        .value("animation", nw::model::GeometryFlag::animation)
        .value("binary", nw::model::GeometryFlag::binary);

    py::enum_<nw::model::GeometryType>(nw, "MdlGeometryType")
        .value("geometry", nw::model::GeometryType::geometry)
        .value("model", nw::model::GeometryType::model)
        .value("animation", nw::model::GeometryType::animation);

    py::enum_<nw::model::ModelClass>(nw, "MdlClassification")
        .value("invalid", nw::model::ModelClass::invalid)
        .value("effect", nw::model::ModelClass::effect)
        .value("tile", nw::model::ModelClass::tile)
        .value("character", nw::model::ModelClass::character)
        .value("door", nw::model::ModelClass::door)
        .value("item", nw::model::ModelClass::item)
        .value("gui", nw::model::ModelClass::gui);

    py::class_<nw::model::ControllerType>(nw, "MdlControllerType")
        .def_ro_static("position", &nw::model::ControllerType::Position)
        .def_ro_static("orientation", &nw::model::ControllerType::Orientation)
        .def_ro_static("scale", &nw::model::ControllerType::Scale)
        .def_ro_static("wirecolor", &nw::model::ControllerType::Wirecolor)
        .def_ro_static("color", &nw::model::ControllerType::Color)
        .def_ro_static("radius", &nw::model::ControllerType::Radius)
        .def_ro_static("shadow_radius", &nw::model::ControllerType::ShadowRadius)
        .def_ro_static("vertical_displacement", &nw::model::ControllerType::VerticalDisplacement)
        .def_ro_static("multiplier", &nw::model::ControllerType::Multiplier)
        .def_ro_static("alpha_end", &nw::model::ControllerType::AlphaEnd)
        .def_ro_static("alpha_start", &nw::model::ControllerType::AlphaStart)
        .def_ro_static("birthrate", &nw::model::ControllerType::BirthRate)
        .def_ro_static("bounce_co", &nw::model::ControllerType::Bounce_Co)
        .def_ro_static("color_end", &nw::model::ControllerType::ColorEnd)
        .def_ro_static("color_start", &nw::model::ControllerType::ColorStart)
        .def_ro_static("combine_time", &nw::model::ControllerType::CombineTime)
        .def_ro_static("drag", &nw::model::ControllerType::Drag)
        .def_ro_static("fps", &nw::model::ControllerType::FPS)
        .def_ro_static("frame_end", &nw::model::ControllerType::FrameEnd)
        .def_ro_static("frame_start", &nw::model::ControllerType::FrameStart)
        .def_ro_static("grav", &nw::model::ControllerType::Grav)
        .def_ro_static("life_exp", &nw::model::ControllerType::LifeExp)
        .def_ro_static("mass", &nw::model::ControllerType::Mass)
        .def_ro_static("p2p_bezier2", &nw::model::ControllerType::P2P_Bezier2)
        .def_ro_static("p2p_bezier3", &nw::model::ControllerType::P2P_Bezier3)
        .def_ro_static("particle_rot", &nw::model::ControllerType::ParticleRot)
        .def_ro_static("rand_vel", &nw::model::ControllerType::RandVel)
        .def_ro_static("size_start", &nw::model::ControllerType::SizeStart)
        .def_ro_static("size_end", &nw::model::ControllerType::SizeEnd)
        .def_ro_static("size_start_y", &nw::model::ControllerType::SizeStart_Y)
        .def_ro_static("size_end_y", &nw::model::ControllerType::SizeEnd_Y)
        .def_ro_static("spread", &nw::model::ControllerType::Spread)
        .def_ro_static("threshold", &nw::model::ControllerType::Threshold)
        .def_ro_static("velocity", &nw::model::ControllerType::Velocity)
        .def_ro_static("xsize", &nw::model::ControllerType::XSize)
        .def_ro_static("ysize", &nw::model::ControllerType::YSize)
        .def_ro_static("blur_length", &nw::model::ControllerType::BlurLength)
        .def_ro_static("lightning_delay", &nw::model::ControllerType::LightningDelay)
        .def_ro_static("lightning_radius", &nw::model::ControllerType::LightningRadius)
        .def_ro_static("lightning_scale", &nw::model::ControllerType::LightningScale)
        .def_ro_static("lightning_subdiv", &nw::model::ControllerType::LightningSubDiv)
        .def_ro_static("detonate", &nw::model::ControllerType::Detonate)
        .def_ro_static("alpha_mid", &nw::model::ControllerType::AlphaMid)
        .def_ro_static("color_mid", &nw::model::ControllerType::ColorMid)
        .def_ro_static("percent_start", &nw::model::ControllerType::PercentStart)
        .def_ro_static("percent_mid", &nw::model::ControllerType::PercentMid)
        .def_ro_static("percent_end", &nw::model::ControllerType::PercentEnd)
        .def_ro_static("size_mid", &nw::model::ControllerType::SizeMid)
        .def_ro_static("size_mid_y", &nw::model::ControllerType::SizeMid_Y)
        .def_ro_static("self_illum_color", &nw::model::ControllerType::SelfIllumColor)
        .def_ro_static("alpha", &nw::model::ControllerType::Alpha);

    py::class_<nw::model::ControllerKey>(nw, "MdlControllerKey>")
        .def_ro("name", &nw::model::ControllerKey::name)
        .def_ro("type", &nw::model::ControllerKey::type)
        .def_ro("rows", &nw::model::ControllerKey::rows)
        .def_ro("key_offset", &nw::model::ControllerKey::key_offset)
        .def_ro("data_offset", &nw::model::ControllerKey::data_offset)
        .def_ro("columns", &nw::model::ControllerKey::columns)
        .def_ro("is_key", &nw::model::ControllerKey::is_key);

    py::class_<nw::model::Face>(nw, "MdlFace")
        .def_rw("vert_idx", &nw::model::Face::vert_idx)
        .def_rw("shader_group_idx", &nw::model::Face::shader_group_idx)
        .def_rw("tvert_idx", &nw::model::Face::tvert_idx)
        .def_rw("material_idx", &nw::model::Face::material_idx);

    py::class_<nw::model::Node>(nw, "MdlNode")
        .def_ro("name", &nw::model::Node::name)
        .def_ro("type", &nw::model::Node::type)
        .def_ro("inheritcolor", &nw::model::Node::inheritcolor)
        .def_ro("parent", &nw::model::Node::parent)
        .def_ro("children", &nw::model::Node::children)
        .def(
            "get_controller", [](const nw::model::Node& self, uint32_t type, bool is_key) {
                auto key = self.get_controller(type, is_key);
                // This is terrible..
                return py::make_tuple(key.key,
                    is_key ? std::vector<float>(key.time.begin(), key.time.end()) : std::vector<float>(),
                    std::vector<float>(key.data.begin(), key.data.end()));
            },
            py::rv_policy::reference);

    py::class_<nw::model::DummyNode, nw::model::Node>(nw, "MdlDummyNode");

    py::class_<nw::model::CameraNode, nw::model::Node>(nw, "MdlCameraNode");

    py::class_<nw::model::EmitterNode, nw::model::Node>(nw, "MdlEmitterNode")
        .def_rw("blastlength", &nw::model::EmitterNode::blastlength)
        .def_rw("blastradius", &nw::model::EmitterNode::blastradius)
        .def_rw("blend", &nw::model::EmitterNode::blend)
        .def_rw("chunkname", &nw::model::EmitterNode::chunkname)
        .def_rw("deadspace", &nw::model::EmitterNode::deadspace)
        .def_rw("loop", &nw::model::EmitterNode::loop)
        .def_rw("render", &nw::model::EmitterNode::render)
        .def_rw("renderorder", &nw::model::EmitterNode::renderorder)
        .def_rw("spawntype", &nw::model::EmitterNode::spawntype)
        .def_rw("texture", &nw::model::EmitterNode::texture)
        .def_rw("twosidedtex", &nw::model::EmitterNode::twosidedtex)
        .def_rw("update", &nw::model::EmitterNode::update)
        .def_rw("xgrid", &nw::model::EmitterNode::xgrid)
        .def_rw("ygrid", &nw::model::EmitterNode::ygrid)
        .def_rw("flags", &nw::model::EmitterNode::flags)
        .def_rw("render_sel", &nw::model::EmitterNode::render_sel)
        .def_rw("blend_sel", &nw::model::EmitterNode::blend_sel)
        .def_rw("update_sel", &nw::model::EmitterNode::update_sel)
        .def_rw("spawntype_sel", &nw::model::EmitterNode::spawntype_sel)
        .def_rw("opacity", &nw::model::EmitterNode::opacity)
        .def_rw("p2p_type", &nw::model::EmitterNode::p2p_type);

    py::class_<nw::model::LightNode, nw::model::Node>(nw, "MdlLightNode")
        .def_rw("flareradius", &nw::model::LightNode::flareradius)
        .def_rw("multiplier", &nw::model::LightNode::multiplier)
        .def_rw("color", &nw::model::LightNode::color)
        .def_rw("flaresizes", &nw::model::LightNode::flaresizes)
        .def_rw("flarepositions", &nw::model::LightNode::flarepositions)
        .def_rw("flarecolorshifts", &nw::model::LightNode::flarecolorshifts)
        .def_rw("textures", &nw::model::LightNode::textures)
        .def_rw("lightpriority", &nw::model::LightNode::lightpriority)
        .def_rw("ambientonly", &nw::model::LightNode::ambientonly)
        .def_rw("dynamic", &nw::model::LightNode::dynamic)
        .def_rw("affectdynamic", &nw::model::LightNode::affectdynamic)
        .def_rw("shadow", &nw::model::LightNode::shadow)
        .def_rw("generateflare", &nw::model::LightNode::generateflare)
        .def_rw("fadinglight", &nw::model::LightNode::fadinglight);

    py::class_<nw::model::PatchNode, nw::model::Node>(nw, "MdlPatchNode");

    py::class_<nw::model::ReferenceNode, nw::model::Node>(nw, "MdlReferenceNode")
        .def_rw("refmodel", &nw::model::ReferenceNode::refmodel)
        .def_rw("reattachable", &nw::model::ReferenceNode::reattachable);

    py::class_<nw::model::Vertex>(nw, "Vertex")
        .def_rw("position", &nw::model::Vertex::position)
        .def_rw("tex_coords", &nw::model::Vertex::tex_coords)
        .def_rw("normal", &nw::model::Vertex::normal)
        .def_rw("tangent", &nw::model::Vertex::tangent);

    py::class_<nw::model::TrimeshNode, nw::model::Node>(nw, "MdlTrimeshNode")
        .def_rw("ambient", &nw::model::TrimeshNode::ambient)
        .def_rw("beaming", &nw::model::TrimeshNode::beaming)
        .def_rw("bmin", &nw::model::TrimeshNode::bmin)
        .def_rw("bmax", &nw::model::TrimeshNode::bmax)
        .def_rw("bitmap", &nw::model::TrimeshNode::bitmap)
        .def_rw("center", &nw::model::TrimeshNode::center)
        .def_rw("diffuse", &nw::model::TrimeshNode::diffuse)
        .def_rw("materialname", &nw::model::TrimeshNode::materialname)
        .def_rw("render", &nw::model::TrimeshNode::render)
        .def_rw("renderhint", &nw::model::TrimeshNode::renderhint)
        .def_rw("rotatetexture", &nw::model::TrimeshNode::rotatetexture)
        .def_rw("shadow", &nw::model::TrimeshNode::shadow)
        .def_rw("shininess", &nw::model::TrimeshNode::shininess)
        .def_rw("specular", &nw::model::TrimeshNode::specular)
        .def_rw("textures", &nw::model::TrimeshNode::textures)
        .def_rw("tilefade", &nw::model::TrimeshNode::tilefade)
        .def_rw("transparencyhint", &nw::model::TrimeshNode::transparencyhint)
        .def_rw("showdispl", &nw::model::TrimeshNode::showdispl)
        .def_rw("displtype", &nw::model::TrimeshNode::displtype)
        .def_rw("lightmapped", &nw::model::TrimeshNode::lightmapped)
        .def_rw("multimaterial", &nw::model::TrimeshNode::multimaterial)
        .def_rw("colors", &nw::model::TrimeshNode::colors)
        .def_rw("vertices", &nw::model::TrimeshNode::vertices)
        .def_rw("indices", &nw::model::TrimeshNode::indices);

    py::class_<nw::model::SkinVertex>(nw, "SkinVertex")
        .def_rw("position", &nw::model::SkinVertex::position)
        .def_rw("tex_coords", &nw::model::SkinVertex::tex_coords)
        .def_rw("normal", &nw::model::SkinVertex::normal)
        .def_rw("tangent", &nw::model::SkinVertex::tangent)
        .def_rw("bones", &nw::model::SkinVertex::bones)
        .def_rw("weights", &nw::model::SkinVertex::weights);

    py::class_<nw::model::SkinNode, nw::model::TrimeshNode>(nw, "MdlSkinNode")
        .def_rw("vertices", &nw::model::SkinNode::vertices);

    py::class_<nw::model::AnimeshNode, nw::model::TrimeshNode>(nw, "MdlAnimeshNode")
        .def_rw("animtverts", &nw::model::AnimeshNode::animtverts)
        .def_rw("animverts", &nw::model::AnimeshNode::animverts)
        .def_rw("sampleperiod", &nw::model::AnimeshNode::sampleperiod);

    py::class_<nw::model::DanglymeshNode, nw::model::TrimeshNode>(nw, "MdlDanglymeshNode")
        .def_rw("constraints", &nw::model::DanglymeshNode::constraints)
        .def_rw("displacement", &nw::model::DanglymeshNode::displacement)
        .def_rw("period", &nw::model::DanglymeshNode::period)
        .def_rw("tightness", &nw::model::DanglymeshNode::tightness);

    py::class_<nw::model::AABBEntry>(nw, "MdlAABBEntry")
        .def_rw("bmin", &nw::model::AABBEntry::bmin)
        .def_rw("bmax", &nw::model::AABBEntry::bmax)
        .def_rw("leaf_face", &nw::model::AABBEntry::leaf_face)
        .def_rw("plane", &nw::model::AABBEntry::plane);
    // std::unique_ptr<MdlAABBEntry> left;
    // std::unique_ptr<MdlAABBEntry> right;

    py::class_<nw::model::AABBNode, nw::model::TrimeshNode>(nw, "MdlAABBNode")
        .def_rw("entries", &nw::model::AABBNode::entries);

    // -- Geometry ----------------------------------------------------------------

    py::class_<nw::model::Geometry>(nw, "MdlGeometry")
        .def_rw("name", &nw::model::Geometry::name)
        .def_rw("type", &nw::model::Geometry::type)
        .def("__len__", [](const nw::model::Geometry& self) { return self.nodes.size(); })
        .def(
            "__getitem__", [](const nw::model::Geometry& self, size_t index) {
                return self.nodes.at(index).get();
            },
            py::rv_policy::reference)
        .def("nodes", [](const nw::model::Geometry& self) {
            auto pylist = py::list();
            for (auto& ptr : self.nodes) {
                auto pyobj = py::cast(*ptr, py::rv_policy::reference);
                pylist.append(pyobj);
            }
            return py::iter(pylist);
        });

    py::class_<nw::model::AnimationEvent>(nw, "MdlAnimationEvent")
        .def_rw("time", &nw::model::AnimationEvent::time)
        .def_rw("name", &nw::model::AnimationEvent::name);

    py::class_<nw::model::Animation, nw::model::Geometry>(nw, "MdlAnimation")
        .def_rw("length", &nw::model::Animation::length)
        .def_rw("transition_time", &nw::model::Animation::transition_time)
        .def_rw("anim_root", &nw::model::Animation::anim_root)
        .def_ro("events", &nw::model::Animation::events);

    py::class_<nw::model::Model, nw::model::Geometry>(nw, "MdlModel")
        .def_rw("classification", &nw::model::Model::classification)
        .def_rw("ignorefog", &nw::model::Model::ignorefog)
        .def("animation_count", [](const nw::model::Model& self) { return self.animations.size(); })
        .def(
            "get_animation", [](const nw::model::Model& self, size_t index) {
                return self.animations.at(index).get();
            },
            py::rv_policy::reference)
        .def("animations", [](const nw::model::Model& self) {
            auto pylist = py::list();
            for (auto& ptr : self.animations) {
                auto pyobj = py::cast(*ptr, py::rv_policy::reference);
                pylist.append(pyobj);
            }
            return py::iter(pylist);
        })
        .def_prop_ro("supermodel", [](const nw::model::Model& self) { return self.supermodel.get(); }, py::rv_policy::reference)
        .def_rw("bmin", &nw::model::Model::bmin)
        .def_rw("bmax", &nw::model::Model::bmax)
        .def_rw("radius", &nw::model::Model::radius)
        .def_rw("animationscale", &nw::model::Model::animationscale)
        .def_rw("supermodel_name", &nw::model::Model::supermodel_name)
        .def_rw("file_dependency", &nw::model::Model::file_dependency);

    py::class_<nw::model::Mdl>(nw, "Mdl")
        .def_static("from_file", [](const char* path) {
            auto mdl = new nw::model::Mdl{path};
            return mdl;
        })
        .def("valid", &nw::model::Mdl::valid)
        .def_prop_ro("model", [](const nw::model::Mdl& self) { return &self.model; }, py::rv_policy::reference);

    nw.def("load", [](const std::string& resref) {
        auto p = nw::expand_path(resref);
        if (std::filesystem::exists(p)) {
            return new nw::model::Mdl(p);
        } else if (resref.size() <= 16) {
            auto data = nw::kernel::resman().demand({resref, nw::ResourceType::mdl});
            if (data.bytes.size()) {
                return new nw::model::Mdl{std::move(data)};
            }
        }
        LOG_F(ERROR, "unable to locate model '{}'", resref);
        return static_cast<nw::model::Mdl*>(nullptr);
    });
}
