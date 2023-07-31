#include "casters.hpp"
#include "opaque_types.hpp"

#include <nw/kernel/Resources.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/util/platform.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include <filesystem>
#include <string>
#include <utility>
#include <variant>

namespace py = pybind11;

void init_model(py::module& nw)
{
    py::class_<nw::model::NodeFlags>(nw, "MdlNodeFlags")
        .def_readonly_static("header", &nw::model::NodeFlags::header)
        .def_readonly_static("light", &nw::model::NodeFlags::light)
        .def_readonly_static("emitter", &nw::model::NodeFlags::emitter)
        .def_readonly_static("camera", &nw::model::NodeFlags::camera)
        .def_readonly_static("reference", &nw::model::NodeFlags::reference)
        .def_readonly_static("mesh", &nw::model::NodeFlags::mesh)
        .def_readonly_static("skin", &nw::model::NodeFlags::skin)
        .def_readonly_static("anim", &nw::model::NodeFlags::anim)
        .def_readonly_static("dangly", &nw::model::NodeFlags::dangly)
        .def_readonly_static("aabb", &nw::model::NodeFlags::aabb)
        .def_readonly_static("patch", &nw::model::NodeFlags::patch);

    py::class_<nw::model::NodeType>(nw, "MdlNodeType")
        .def_readonly_static("camera", &nw::model::NodeType::camera)
        .def_readonly_static("dummy", &nw::model::NodeType::dummy)
        .def_readonly_static("emitter", &nw::model::NodeType::emitter)
        .def_readonly_static("light", &nw::model::NodeType::light)
        .def_readonly_static("reference", &nw::model::NodeType::reference)
        .def_readonly_static("patch", &nw::model::NodeType::patch)
        .def_readonly_static("trimesh", &nw::model::NodeType::trimesh)
        .def_readonly_static("danglymesh", &nw::model::NodeType::danglymesh)
        .def_readonly_static("skin", &nw::model::NodeType::skin)
        .def_readonly_static("animmesh", &nw::model::NodeType::animmesh)
        .def_readonly_static("aabb", &nw::model::NodeType::aabb);

    py::class_<nw::model::EmitterFlag>(nw, "MdlEmitterFlag")
        .def_readonly_static("affected_by_wind", &nw::model::EmitterFlag::AffectedByWind)
        .def_readonly_static("bounce", &nw::model::EmitterFlag::Bounce)
        .def_readonly_static("inherit_local", &nw::model::EmitterFlag::InheritLocal)
        .def_readonly_static("inherit_part", &nw::model::EmitterFlag::InheritPart)
        .def_readonly_static("inherit_vel", &nw::model::EmitterFlag::InheritVel)
        .def_readonly_static("inherit", &nw::model::EmitterFlag::Inherit)
        .def_readonly_static("is_tinted", &nw::model::EmitterFlag::IsTinted)
        .def_readonly_static("p2p_sel", &nw::model::EmitterFlag::P2PSel)
        .def_readonly_static("p2p", &nw::model::EmitterFlag::P2P)
        .def_readonly_static("random", &nw::model::EmitterFlag::Random)
        .def_readonly_static("splat", &nw::model::EmitterFlag::Splat);

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
        .def_readonly_static("position", &nw::model::ControllerType::Position)
        .def_readonly_static("orientation", &nw::model::ControllerType::Orientation)
        .def_readonly_static("scale", &nw::model::ControllerType::Scale)
        .def_readonly_static("wirecolor", &nw::model::ControllerType::Wirecolor)
        .def_readonly_static("color", &nw::model::ControllerType::Color)
        .def_readonly_static("radius", &nw::model::ControllerType::Radius)
        .def_readonly_static("shadow_radius", &nw::model::ControllerType::ShadowRadius)
        .def_readonly_static("vertical_displacement", &nw::model::ControllerType::VerticalDisplacement)
        .def_readonly_static("multiplier", &nw::model::ControllerType::Multiplier)
        .def_readonly_static("alpha_end", &nw::model::ControllerType::AlphaEnd)
        .def_readonly_static("alpha_start", &nw::model::ControllerType::AlphaStart)
        .def_readonly_static("birthrate", &nw::model::ControllerType::BirthRate)
        .def_readonly_static("bounce_co", &nw::model::ControllerType::Bounce_Co)
        .def_readonly_static("color_end", &nw::model::ControllerType::ColorEnd)
        .def_readonly_static("color_start", &nw::model::ControllerType::ColorStart)
        .def_readonly_static("combine_time", &nw::model::ControllerType::CombineTime)
        .def_readonly_static("drag", &nw::model::ControllerType::Drag)
        .def_readonly_static("fps", &nw::model::ControllerType::FPS)
        .def_readonly_static("frame_end", &nw::model::ControllerType::FrameEnd)
        .def_readonly_static("frame_start", &nw::model::ControllerType::FrameStart)
        .def_readonly_static("grav", &nw::model::ControllerType::Grav)
        .def_readonly_static("life_exp", &nw::model::ControllerType::LifeExp)
        .def_readonly_static("mass", &nw::model::ControllerType::Mass)
        .def_readonly_static("p2p_bezier2", &nw::model::ControllerType::P2P_Bezier2)
        .def_readonly_static("p2p_bezier3", &nw::model::ControllerType::P2P_Bezier3)
        .def_readonly_static("particle_rot", &nw::model::ControllerType::ParticleRot)
        .def_readonly_static("rand_vel", &nw::model::ControllerType::RandVel)
        .def_readonly_static("size_start", &nw::model::ControllerType::SizeStart)
        .def_readonly_static("size_end", &nw::model::ControllerType::SizeEnd)
        .def_readonly_static("size_start_y", &nw::model::ControllerType::SizeStart_Y)
        .def_readonly_static("size_end_y", &nw::model::ControllerType::SizeEnd_Y)
        .def_readonly_static("spread", &nw::model::ControllerType::Spread)
        .def_readonly_static("threshold", &nw::model::ControllerType::Threshold)
        .def_readonly_static("velocity", &nw::model::ControllerType::Velocity)
        .def_readonly_static("xsize", &nw::model::ControllerType::XSize)
        .def_readonly_static("ysize", &nw::model::ControllerType::YSize)
        .def_readonly_static("blur_length", &nw::model::ControllerType::BlurLength)
        .def_readonly_static("lightning_delay", &nw::model::ControllerType::LightningDelay)
        .def_readonly_static("lightning_radius", &nw::model::ControllerType::LightningRadius)
        .def_readonly_static("lightning_scale", &nw::model::ControllerType::LightningScale)
        .def_readonly_static("lightning_subdiv", &nw::model::ControllerType::LightningSubDiv)
        .def_readonly_static("detonate", &nw::model::ControllerType::Detonate)
        .def_readonly_static("alpha_mid", &nw::model::ControllerType::AlphaMid)
        .def_readonly_static("color_mid", &nw::model::ControllerType::ColorMid)
        .def_readonly_static("percent_start", &nw::model::ControllerType::PercentStart)
        .def_readonly_static("percent_mid", &nw::model::ControllerType::PercentMid)
        .def_readonly_static("percent_end", &nw::model::ControllerType::PercentEnd)
        .def_readonly_static("size_mid", &nw::model::ControllerType::SizeMid)
        .def_readonly_static("size_mid_y", &nw::model::ControllerType::SizeMid_Y)
        .def_readonly_static("self_illum_color", &nw::model::ControllerType::SelfIllumColor)
        .def_readonly_static("alpha", &nw::model::ControllerType::Alpha);

    py::class_<nw::model::ControllerKey>(nw, "MdlControllerKey>")
        .def_readwrite("name", &nw::model::ControllerKey::name)
        .def_readwrite("type", &nw::model::ControllerKey::type)
        .def_readwrite("rows", &nw::model::ControllerKey::rows)
        .def_readwrite("key_offset", &nw::model::ControllerKey::key_offset)
        .def_readwrite("data_offset", &nw::model::ControllerKey::data_offset)
        .def_readwrite("columns", &nw::model::ControllerKey::columns)
        .def_readwrite("is_key", &nw::model::ControllerKey::is_key);

    py::class_<nw::model::Face>(nw, "MdlFace")
        .def_readwrite("vert_idx", &nw::model::Face::vert_idx)
        .def_readwrite("shader_group_idx", &nw::model::Face::shader_group_idx)
        .def_readwrite("tvert_idx", &nw::model::Face::tvert_idx)
        .def_readwrite("material_idx", &nw::model::Face::material_idx);

    py::class_<nw::model::Node>(nw, "MdlNode")
        .def_readonly("name", &nw::model::Node::name)
        .def_readonly("type", &nw::model::Node::type)
        .def_readonly("inheritcolor", &nw::model::Node::inheritcolor)
        .def_readonly("parent", &nw::model::Node::parent)
        .def_readonly("children", &nw::model::Node::children)
        .def_readonly("controller_keys", &nw::model::Node::controller_keys)
        .def_readonly("controller_data", &nw::model::Node::controller_data)
        .def(
            "get_controller", [](const nw::model::Node& self, uint32_t type, bool is_key) {
                auto key = self.get_controller(type, is_key);
                // This is terrible..
                return py::make_tuple(key.key,
                    is_key ? std::vector<float>(key.time.begin(), key.time.end()) : std::vector<float>(),
                    std::vector<float>(key.data.begin(), key.data.end()));
            },
            py::return_value_policy::reference);

    py::class_<nw::model::DummyNode, nw::model::Node>(nw, "MdlDummyNode");

    py::class_<nw::model::CameraNode, nw::model::Node>(nw, "MdlCameraNode");

    py::class_<nw::model::EmitterNode, nw::model::Node>(nw, "MdlEmitterNode")
        .def_readwrite("blastlength", &nw::model::EmitterNode::blastlength)
        .def_readwrite("blastradius", &nw::model::EmitterNode::blastradius)
        .def_readwrite("blend", &nw::model::EmitterNode::blend)
        .def_readwrite("chunkname", &nw::model::EmitterNode::chunkname)
        .def_readwrite("deadspace", &nw::model::EmitterNode::deadspace)
        .def_readwrite("loop", &nw::model::EmitterNode::loop)
        .def_readwrite("render", &nw::model::EmitterNode::render)
        .def_readwrite("renderorder", &nw::model::EmitterNode::renderorder)
        .def_readwrite("spawntype", &nw::model::EmitterNode::spawntype)
        .def_readwrite("texture", &nw::model::EmitterNode::texture)
        .def_readwrite("twosidedtex", &nw::model::EmitterNode::twosidedtex)
        .def_readwrite("update", &nw::model::EmitterNode::update)
        .def_readwrite("xgrid", &nw::model::EmitterNode::xgrid)
        .def_readwrite("ygrid", &nw::model::EmitterNode::ygrid)
        .def_readwrite("flags", &nw::model::EmitterNode::flags)
        .def_readwrite("render_sel", &nw::model::EmitterNode::render_sel)
        .def_readwrite("blend_sel", &nw::model::EmitterNode::blend_sel)
        .def_readwrite("update_sel", &nw::model::EmitterNode::update_sel)
        .def_readwrite("spawntype_sel", &nw::model::EmitterNode::spawntype_sel)
        .def_readwrite("opacity", &nw::model::EmitterNode::opacity)
        .def_readwrite("p2p_type", &nw::model::EmitterNode::p2p_type);

    py::class_<nw::model::LightNode, nw::model::Node>(nw, "MdlLightNode")
        .def_readwrite("flareradius", &nw::model::LightNode::flareradius)
        .def_readwrite("multiplier", &nw::model::LightNode::multiplier)
        .def_readwrite("color", &nw::model::LightNode::color)
        .def_readwrite("flaresizes", &nw::model::LightNode::flaresizes)
        .def_readwrite("flarepositions", &nw::model::LightNode::flarepositions)
        .def_readwrite("flarecolorshifts", &nw::model::LightNode::flarecolorshifts)
        .def_readwrite("textures", &nw::model::LightNode::textures)
        .def_readwrite("lightpriority", &nw::model::LightNode::lightpriority)
        .def_readwrite("ambientonly", &nw::model::LightNode::ambientonly)
        .def_readwrite("dynamic", &nw::model::LightNode::dynamic)
        .def_readwrite("affectdynamic", &nw::model::LightNode::affectdynamic)
        .def_readwrite("shadow", &nw::model::LightNode::shadow)
        .def_readwrite("generateflare", &nw::model::LightNode::generateflare)
        .def_readwrite("fadinglight", &nw::model::LightNode::fadinglight);

    py::class_<nw::model::PatchNode, nw::model::Node>(nw, "MdlPatchNode");

    py::class_<nw::model::ReferenceNode, nw::model::Node>(nw, "MdlReferenceNode")
        .def_readwrite("refmodel", &nw::model::ReferenceNode::refmodel)
        .def_readwrite("reattachable", &nw::model::ReferenceNode::reattachable);

    py::class_<nw::model::Vertex>(nw, "Vertex")
        .def_readwrite("position", &nw::model::Vertex::position)
        .def_readwrite("tex_coords", &nw::model::Vertex::tex_coords)
        .def_readwrite("normal", &nw::model::Vertex::normal)
        .def_readwrite("tangent", &nw::model::Vertex::tangent);

    py::class_<nw::model::TrimeshNode, nw::model::Node>(nw, "MdlTrimeshNode")
        .def_readwrite("ambient", &nw::model::TrimeshNode::ambient)
        .def_readwrite("beaming", &nw::model::TrimeshNode::beaming)
        .def_readwrite("bmin", &nw::model::TrimeshNode::bmin)
        .def_readwrite("bmax", &nw::model::TrimeshNode::bmax)
        .def_readwrite("bitmap", &nw::model::TrimeshNode::bitmap)
        .def_readwrite("center", &nw::model::TrimeshNode::center)
        .def_readwrite("diffuse", &nw::model::TrimeshNode::diffuse)
        .def_readwrite("materialname", &nw::model::TrimeshNode::materialname)
        .def_readwrite("render", &nw::model::TrimeshNode::render)
        .def_readwrite("renderhint", &nw::model::TrimeshNode::renderhint)
        .def_readwrite("rotatetexture", &nw::model::TrimeshNode::rotatetexture)
        .def_readwrite("shadow", &nw::model::TrimeshNode::shadow)
        .def_readwrite("shininess", &nw::model::TrimeshNode::shininess)
        .def_readwrite("specular", &nw::model::TrimeshNode::specular)
        .def_readwrite("textures", &nw::model::TrimeshNode::textures)
        .def_readwrite("tilefade", &nw::model::TrimeshNode::tilefade)
        .def_readwrite("transparencyhint", &nw::model::TrimeshNode::transparencyhint)
        .def_readwrite("showdispl", &nw::model::TrimeshNode::showdispl)
        .def_readwrite("displtype", &nw::model::TrimeshNode::displtype)
        .def_readwrite("lightmapped", &nw::model::TrimeshNode::lightmapped)
        .def_readwrite("multimaterial", &nw::model::TrimeshNode::multimaterial)
        .def_readwrite("colors", &nw::model::TrimeshNode::colors)
        .def_readwrite("vertices", &nw::model::TrimeshNode::vertices)
        .def_readwrite("indices", &nw::model::TrimeshNode::indices);

    py::class_<nw::model::SkinVertex>(nw, "SkinVertex")
        .def_readwrite("position", &nw::model::SkinVertex::position)
        .def_readwrite("tex_coords", &nw::model::SkinVertex::tex_coords)
        .def_readwrite("normal", &nw::model::SkinVertex::normal)
        .def_readwrite("tangent", &nw::model::SkinVertex::tangent)
        .def_readwrite("bones", &nw::model::SkinVertex::bones)
        .def_readwrite("weights", &nw::model::SkinVertex::weights);

    py::class_<nw::model::SkinNode, nw::model::TrimeshNode>(nw, "MdlSkinNode")
        .def_readwrite("vertices", &nw::model::SkinNode::vertices);

    py::class_<nw::model::AnimeshNode, nw::model::TrimeshNode>(nw, "MdlAnimeshNode")
        .def_readwrite("animtverts", &nw::model::AnimeshNode::animtverts)
        .def_readwrite("animverts", &nw::model::AnimeshNode::animverts)
        .def_readwrite("sampleperiod", &nw::model::AnimeshNode::sampleperiod);

    py::class_<nw::model::DanglymeshNode, nw::model::TrimeshNode>(nw, "MdlDanglymeshNode")
        .def_readwrite("constraints", &nw::model::DanglymeshNode::constraints)
        .def_readwrite("displacement", &nw::model::DanglymeshNode::displacement)
        .def_readwrite("period", &nw::model::DanglymeshNode::period)
        .def_readwrite("tightness", &nw::model::DanglymeshNode::tightness);

    py::class_<nw::model::AABBEntry>(nw, "MdlAABBEntry")
        .def_readwrite("bmin", &nw::model::AABBEntry::bmin)
        .def_readwrite("bmax", &nw::model::AABBEntry::bmax)
        .def_readwrite("leaf_face", &nw::model::AABBEntry::leaf_face)
        .def_readwrite("plane", &nw::model::AABBEntry::plane);
    // std::unique_ptr<MdlAABBEntry> left;
    // std::unique_ptr<MdlAABBEntry> right;

    py::class_<nw::model::AABBNode, nw::model::TrimeshNode>(nw, "MdlAABBNode")
        .def_readwrite("entries", &nw::model::AABBNode::entries);

    // -- Geometry ----------------------------------------------------------------

    py::class_<nw::model::Geometry>(nw, "MdlGeometry")
        .def_readwrite("name", &nw::model::Geometry::name)
        .def_readwrite("type", &nw::model::Geometry::type)
        .def("__len__", [](const nw::model::Geometry& self) { return self.nodes.size(); })
        .def(
            "__getitem__", [](const nw::model::Geometry& self, size_t index) {
                return self.nodes.at(index).get();
            },
            py::return_value_policy::reference)
        .def("nodes", [](const nw::model::Geometry& self) {
            auto pylist = py::list();
            for (auto& ptr : self.nodes) {
                auto pyobj = py::cast(*ptr, py::return_value_policy::reference);
                pylist.append(pyobj);
            }
            return py::iter(pylist);
        });

    py::class_<nw::model::AnimationEvent>(nw, "MdlAnimationEvent")
        .def_readwrite("time", &nw::model::AnimationEvent::time)
        .def_readwrite("name", &nw::model::AnimationEvent::name);

    py::class_<nw::model::Animation, nw::model::Geometry>(nw, "MdlAnimation")
        .def_readwrite("length", &nw::model::Animation::length)
        .def_readwrite("transition_time", &nw::model::Animation::transition_time)
        .def_readwrite("anim_root", &nw::model::Animation::anim_root)
        .def_readonly("events", &nw::model::Animation::events);

    py::class_<nw::model::Model, nw::model::Geometry>(nw, "MdlModel")
        .def_readwrite("classification", &nw::model::Model::classification)
        .def_readwrite("ignorefog", &nw::model::Model::ignorefog)
        .def("animation_count", [](const nw::model::Model& self) { return self.animations.size(); })
        .def(
            "get_animation", [](const nw::model::Model& self, size_t index) {
                return self.animations.at(index).get();
            },
            py::return_value_policy::reference)
        .def("animations", [](const nw::model::Model& self) {
            auto pylist = py::list();
            for (auto& ptr : self.animations) {
                auto pyobj = py::cast(*ptr, py::return_value_policy::reference);
                pylist.append(pyobj);
            }
            return py::iter(pylist);
        })
        .def_property_readonly(
            "supermodel", [](const nw::model::Model& self) { return self.supermodel.get(); }, py::return_value_policy::reference)
        .def_readwrite("bmin", &nw::model::Model::bmin)
        .def_readwrite("bmax", &nw::model::Model::bmax)
        .def_readwrite("radius", &nw::model::Model::radius)
        .def_readwrite("animationscale", &nw::model::Model::animationscale)
        .def_readwrite("supermodel_name", &nw::model::Model::supermodel_name)
        .def_readwrite("file_dependency", &nw::model::Model::file_dependency);

    py::class_<nw::model::Mdl>(nw, "Mdl")
        .def_static("from_file", [](const char* path) {
            auto mdl = new nw::model::Mdl{path};
            return mdl;
        })
        .def("valid", &nw::model::Mdl::valid)
        .def_property_readonly(
            "model", [](const nw::model::Mdl& self) {
                return &self.model;
            },
            py::return_value_policy::reference);

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
