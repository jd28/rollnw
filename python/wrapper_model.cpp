#include <nw/model/Mdl.hpp>
#include <nw/model/MdlTextParser.hpp>

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
    py::class_<nw::MdlNodeFlags>(nw, "MdlNodeFlags")
        .def_readonly_static("header", &nw::MdlNodeFlags::header)
        .def_readonly_static("light", &nw::MdlNodeFlags::light)
        .def_readonly_static("emitter", &nw::MdlNodeFlags::emitter)
        .def_readonly_static("camera", &nw::MdlNodeFlags::camera)
        .def_readonly_static("reference", &nw::MdlNodeFlags::reference)
        .def_readonly_static("mesh", &nw::MdlNodeFlags::mesh)
        .def_readonly_static("skin", &nw::MdlNodeFlags::skin)
        .def_readonly_static("anim", &nw::MdlNodeFlags::anim)
        .def_readonly_static("dangly", &nw::MdlNodeFlags::dangly)
        .def_readonly_static("aabb", &nw::MdlNodeFlags::aabb)
        .def_readonly_static("patch", &nw::MdlNodeFlags::patch);

    py::class_<nw::MdlNodeType>(nw, "MdlNodeType")
        .def_readonly_static("camera", &nw::MdlNodeType::camera)
        .def_readonly_static("dummy", &nw::MdlNodeType::dummy)
        .def_readonly_static("emitter", &nw::MdlNodeType::emitter)
        .def_readonly_static("light", &nw::MdlNodeType::light)
        .def_readonly_static("reference", &nw::MdlNodeType::reference)
        .def_readonly_static("patch", &nw::MdlNodeType::patch)
        .def_readonly_static("trimesh", &nw::MdlNodeType::trimesh)
        .def_readonly_static("danglymesh", &nw::MdlNodeType::danglymesh)
        .def_readonly_static("skin", &nw::MdlNodeType::skin)
        .def_readonly_static("animmesh", &nw::MdlNodeType::animmesh)
        .def_readonly_static("aabb", &nw::MdlNodeType::aabb);

    py::class_<nw::MdlEmitterFlag>(nw, "MdlEmitterFlag")
        .def_readonly_static("affected_by_wind", &nw::MdlEmitterFlag::AffectedByWind)
        .def_readonly_static("bounce", &nw::MdlEmitterFlag::Bounce)
        .def_readonly_static("inherit_local", &nw::MdlEmitterFlag::InheritLocal)
        .def_readonly_static("inherit_part", &nw::MdlEmitterFlag::InheritPart)
        .def_readonly_static("inherit_vel", &nw::MdlEmitterFlag::InheritVel)
        .def_readonly_static("inherit", &nw::MdlEmitterFlag::Inherit)
        .def_readonly_static("is_tinted", &nw::MdlEmitterFlag::IsTinted)
        .def_readonly_static("p2p_sel", &nw::MdlEmitterFlag::P2PSel)
        .def_readonly_static("p2p", &nw::MdlEmitterFlag::P2P)
        .def_readonly_static("random", &nw::MdlEmitterFlag::Random)
        .def_readonly_static("splat", &nw::MdlEmitterFlag::Splat);

    py::enum_<nw::MdlTriangleMode>(nw, "MdlTriangleMode")
        .value("triangle", nw::MdlTriangleMode::triangle)
        .value("triangle_strip", nw::MdlTriangleMode::triangle_strip);

    py::enum_<nw::MdlGeometryFlag>(nw, "MdlGeometryFlag")
        .value("geometry", nw::MdlGeometryFlag::geometry)
        .value("model", nw::MdlGeometryFlag::model)
        .value("animation", nw::MdlGeometryFlag::animation)
        .value("binary", nw::MdlGeometryFlag::binary);

    py::enum_<nw::MdlGeometryType>(nw, "MdlGeometryType")
        .value("geometry", nw::MdlGeometryType::geometry)
        .value("model", nw::MdlGeometryType::model)
        .value("animation", nw::MdlGeometryType::animation);

    py::enum_<nw::MdlModelClass>(nw, "MdlClassification")
        .value("invalid", nw::MdlModelClass::invalid)
        .value("effect", nw::MdlModelClass::effect)
        .value("tile", nw::MdlModelClass::tile)
        .value("character", nw::MdlModelClass::character)
        .value("door", nw::MdlModelClass::door)
        .value("item", nw::MdlModelClass::item)
        .value("gui", nw::MdlModelClass::gui);

    py::class_<nw::MdlControllerType>(nw, "MdlControllerType")
        .def_readonly_static("position", &nw::MdlControllerType::Position)
        .def_readonly_static("orientation", &nw::MdlControllerType::Orientation)
        .def_readonly_static("scale", &nw::MdlControllerType::Scale)
        .def_readonly_static("wirecolor", &nw::MdlControllerType::Wirecolor)
        .def_readonly_static("color", &nw::MdlControllerType::Color)
        .def_readonly_static("radius", &nw::MdlControllerType::Radius)
        .def_readonly_static("shadow_radius", &nw::MdlControllerType::ShadowRadius)
        .def_readonly_static("vertical_displacement", &nw::MdlControllerType::VerticalDisplacement)
        .def_readonly_static("multiplier", &nw::MdlControllerType::Multiplier)
        .def_readonly_static("alpha_end", &nw::MdlControllerType::AlphaEnd)
        .def_readonly_static("alpha_start", &nw::MdlControllerType::AlphaStart)
        .def_readonly_static("birthrate", &nw::MdlControllerType::BirthRate)
        .def_readonly_static("bounce_co", &nw::MdlControllerType::Bounce_Co)
        .def_readonly_static("color_end", &nw::MdlControllerType::ColorEnd)
        .def_readonly_static("color_start", &nw::MdlControllerType::ColorStart)
        .def_readonly_static("combine_time", &nw::MdlControllerType::CombineTime)
        .def_readonly_static("drag", &nw::MdlControllerType::Drag)
        .def_readonly_static("fps", &nw::MdlControllerType::FPS)
        .def_readonly_static("frame_end", &nw::MdlControllerType::FrameEnd)
        .def_readonly_static("frame_start", &nw::MdlControllerType::FrameStart)
        .def_readonly_static("grav", &nw::MdlControllerType::Grav)
        .def_readonly_static("life_exp", &nw::MdlControllerType::LifeExp)
        .def_readonly_static("mass", &nw::MdlControllerType::Mass)
        .def_readonly_static("p2p_bezier2", &nw::MdlControllerType::P2P_Bezier2)
        .def_readonly_static("p2p_bezier3", &nw::MdlControllerType::P2P_Bezier3)
        .def_readonly_static("particle_rot", &nw::MdlControllerType::ParticleRot)
        .def_readonly_static("rand_vel", &nw::MdlControllerType::RandVel)
        .def_readonly_static("size_start", &nw::MdlControllerType::SizeStart)
        .def_readonly_static("size_end", &nw::MdlControllerType::SizeEnd)
        .def_readonly_static("size_start_y", &nw::MdlControllerType::SizeStart_Y)
        .def_readonly_static("size_end_y", &nw::MdlControllerType::SizeEnd_Y)
        .def_readonly_static("spread", &nw::MdlControllerType::Spread)
        .def_readonly_static("threshold", &nw::MdlControllerType::Threshold)
        .def_readonly_static("velocity", &nw::MdlControllerType::Velocity)
        .def_readonly_static("xsize", &nw::MdlControllerType::XSize)
        .def_readonly_static("ysize", &nw::MdlControllerType::YSize)
        .def_readonly_static("blur_length", &nw::MdlControllerType::BlurLength)
        .def_readonly_static("lightning_delay", &nw::MdlControllerType::LightningDelay)
        .def_readonly_static("lightning_radius", &nw::MdlControllerType::LightningRadius)
        .def_readonly_static("lightning_scale", &nw::MdlControllerType::LightningScale)
        .def_readonly_static("lightning_subdiv", &nw::MdlControllerType::LightningSubDiv)
        .def_readonly_static("detonate", &nw::MdlControllerType::Detonate)
        .def_readonly_static("alpha_mid", &nw::MdlControllerType::AlphaMid)
        .def_readonly_static("color_mid", &nw::MdlControllerType::ColorMid)
        .def_readonly_static("percent_start", &nw::MdlControllerType::PercentStart)
        .def_readonly_static("percent_mid", &nw::MdlControllerType::PercentMid)
        .def_readonly_static("percent_end", &nw::MdlControllerType::PercentEnd)
        .def_readonly_static("size_mid", &nw::MdlControllerType::SizeMid)
        .def_readonly_static("size_mid_y", &nw::MdlControllerType::SizeMid_Y)
        .def_readonly_static("self_illum_color", &nw::MdlControllerType::SelfIllumColor)
        .def_readonly_static("alpha", &nw::MdlControllerType::Alpha);

    py::class_<nw::MdlControllerKey>(nw, "MdlControllerKey>")
        .def_readwrite("name", &nw::MdlControllerKey::name)
        .def_readwrite("type", &nw::MdlControllerKey::type)
        .def_readwrite("rows", &nw::MdlControllerKey::rows)
        .def_readwrite("key_offset", &nw::MdlControllerKey::key_offset)
        .def_readwrite("data_offset", &nw::MdlControllerKey::data_offset)
        .def_readwrite("columns", &nw::MdlControllerKey::columns);

    py::class_<nw::MdlFace>(nw, "MdlFace")
        .def_readwrite("vert_idx", &nw::MdlFace::vert_idx)
        .def_readwrite("shader_group_idx", &nw::MdlFace::shader_group_idx)
        .def_readwrite("tvert_idx", &nw::MdlFace::tvert_idx)
        .def_readwrite("material_idx", &nw::MdlFace::material_idx);

    py::class_<nw::MdlNode>(nw, "MdlNode")
        .def_readonly("name", &nw::MdlNode::name)
        .def_readonly("type", &nw::MdlNode::type)
        .def_readonly("inheritcolor", &nw::MdlNode::inheritcolor)
        .def_readonly("parent", &nw::MdlNode::parent)
        .def_readonly("children", &nw::MdlNode::children)
        .def_readonly("controller_keys", &nw::MdlNode::controller_keys)
        .def_readonly("controller_data", &nw::MdlNode::controller_data)
        .def(
            "get_controller", [](const nw::MdlNode& self, uint32_t type) {
                auto [key, data] = self.get_controller(type);
                // This is terrible..
                return std::make_tuple(key, std::vector<float>(data.begin(), data.end()));
            },
            py::return_value_policy::reference);

    py::class_<nw::MdlDummyNode, nw::MdlNode>(nw, "MdlDummyNode");

    py::class_<nw::MdlCameraNode, nw::MdlNode>(nw, "MdlCameraNode");

    py::class_<nw::MdlEmitterNode, nw::MdlNode>(nw, "MdlEmitterNode")
        .def_readwrite("blastlength", &nw::MdlEmitterNode::blastlength)
        .def_readwrite("blastradius", &nw::MdlEmitterNode::blastradius)
        .def_readwrite("blend", &nw::MdlEmitterNode::blend)
        .def_readwrite("chunkname", &nw::MdlEmitterNode::chunkname)
        .def_readwrite("deadspace", &nw::MdlEmitterNode::deadspace)
        .def_readwrite("loop", &nw::MdlEmitterNode::loop)
        .def_readwrite("render", &nw::MdlEmitterNode::render)
        .def_readwrite("renderorder", &nw::MdlEmitterNode::renderorder)
        .def_readwrite("spawntype", &nw::MdlEmitterNode::spawntype)
        .def_readwrite("texture", &nw::MdlEmitterNode::texture)
        .def_readwrite("twosidedtex", &nw::MdlEmitterNode::twosidedtex)
        .def_readwrite("update", &nw::MdlEmitterNode::update)
        .def_readwrite("xgrid", &nw::MdlEmitterNode::xgrid)
        .def_readwrite("ygrid", &nw::MdlEmitterNode::ygrid)
        .def_readwrite("flags", &nw::MdlEmitterNode::flags)
        .def_readwrite("render_sel", &nw::MdlEmitterNode::render_sel)
        .def_readwrite("blend_sel", &nw::MdlEmitterNode::blend_sel)
        .def_readwrite("update_sel", &nw::MdlEmitterNode::update_sel)
        .def_readwrite("spawntype_sel", &nw::MdlEmitterNode::spawntype_sel)
        .def_readwrite("opacity", &nw::MdlEmitterNode::opacity)
        .def_readwrite("p2p_type", &nw::MdlEmitterNode::p2p_type);

    py::class_<nw::MdlLightNode, nw::MdlNode>(nw, "MdlLightNode")
        .def_readwrite("flareradius", &nw::MdlLightNode::flareradius)
        .def_readwrite("multiplier", &nw::MdlLightNode::multiplier)
        .def_readwrite("color", &nw::MdlLightNode::color)
        .def_readwrite("flaresizes", &nw::MdlLightNode::flaresizes)
        .def_readwrite("flarepositions", &nw::MdlLightNode::flarepositions)
        .def_readwrite("flarecolorshifts", &nw::MdlLightNode::flarecolorshifts)
        .def_readwrite("textures", &nw::MdlLightNode::textures)
        .def_readwrite("lightpriority", &nw::MdlLightNode::lightpriority)
        .def_readwrite("ambientonly", &nw::MdlLightNode::ambientonly)
        .def_readwrite("dynamic", &nw::MdlLightNode::dynamic)
        .def_readwrite("affectdynamic", &nw::MdlLightNode::affectdynamic)
        .def_readwrite("shadow", &nw::MdlLightNode::shadow)
        .def_readwrite("generateflare", &nw::MdlLightNode::generateflare)
        .def_readwrite("fadinglight", &nw::MdlLightNode::fadinglight);

    py::class_<nw::MdlPatchNode, nw::MdlNode>(nw, "MdlPatchNode");

    py::class_<nw::MdlReferenceNode, nw::MdlNode>(nw, "MdlReferenceNode")
        .def_readwrite("refmodel", &nw::MdlReferenceNode::refmodel)
        .def_readwrite("reattachable", &nw::MdlReferenceNode::reattachable);

    py::class_<nw::MdlTrimeshNode, nw::MdlNode>(nw, "MdlTrimeshNode")
        .def_readwrite("ambient", &nw::MdlTrimeshNode::ambient)
        .def_readwrite("beaming", &nw::MdlTrimeshNode::beaming)
        .def_readwrite("bmin", &nw::MdlTrimeshNode::bmin)
        .def_readwrite("bmax", &nw::MdlTrimeshNode::bmax)
        .def_readwrite("bitmap", &nw::MdlTrimeshNode::bitmap)
        .def_readwrite("center", &nw::MdlTrimeshNode::center)
        .def_readwrite("diffuse", &nw::MdlTrimeshNode::diffuse)
        .def_readwrite("faces", &nw::MdlTrimeshNode::faces)
        .def_readwrite("materialname", &nw::MdlTrimeshNode::materialname)
        .def_readwrite("render", &nw::MdlTrimeshNode::render)
        .def_readwrite("renderhint", &nw::MdlTrimeshNode::renderhint)
        .def_readwrite("rotatetexture", &nw::MdlTrimeshNode::rotatetexture)
        .def_readwrite("shadow", &nw::MdlTrimeshNode::shadow)
        .def_readwrite("shininess", &nw::MdlTrimeshNode::shininess)
        .def_readwrite("specular", &nw::MdlTrimeshNode::specular)
        .def_readwrite("textures", &nw::MdlTrimeshNode::textures)
        .def_readwrite("tilefade", &nw::MdlTrimeshNode::tilefade)
        .def_readwrite("transparencyhint", &nw::MdlTrimeshNode::transparencyhint)
        .def_readwrite("showdispl", &nw::MdlTrimeshNode::showdispl)
        .def_readwrite("displtype", &nw::MdlTrimeshNode::displtype)
        .def_readwrite("lightmapped", &nw::MdlTrimeshNode::lightmapped)
        .def_readwrite("multimaterial", &nw::MdlTrimeshNode::multimaterial)
        .def_readwrite("colors", &nw::MdlTrimeshNode::colors)
        .def_readwrite("verts", &nw::MdlTrimeshNode::verts)
        .def_readwrite("tverts", &nw::MdlTrimeshNode::tverts)
        .def_readwrite("normals", &nw::MdlTrimeshNode::normals)
        .def_readwrite("tangents", &nw::MdlTrimeshNode::tangents);

    py::class_<nw::MdlSkinWeight>(nw, "MdlSkinWeight")
        .def_readwrite("bones", &nw::MdlSkinWeight::bones)
        .def_readwrite("weights", &nw::MdlSkinWeight::weights);

    py::class_<nw::MdlSkinNode, nw::MdlTrimeshNode>(nw, "MdlSkinNode")
        .def_readwrite("weights", &nw::MdlSkinNode::weights);

    py::class_<nw::MdlAnimeshNode, nw::MdlTrimeshNode>(nw, "MdlAnimeshNode")
        .def_readwrite("animtverts", &nw::MdlAnimeshNode::animtverts)
        .def_readwrite("animverts", &nw::MdlAnimeshNode::animverts)
        .def_readwrite("sampleperiod", &nw::MdlAnimeshNode::sampleperiod);

    py::class_<nw::MdlDanglymeshNode, nw::MdlTrimeshNode>(nw, "MdlDanglymeshNode")
        .def_readwrite("constraints", &nw::MdlDanglymeshNode::constraints)
        .def_readwrite("displacement", &nw::MdlDanglymeshNode::displacement)
        .def_readwrite("period", &nw::MdlDanglymeshNode::period)
        .def_readwrite("tightness", &nw::MdlDanglymeshNode::tightness);

    py::class_<nw::MdlAABBEntry>(nw, "MdlAABBEntry")
        .def_readwrite("bmin", &nw::MdlAABBEntry::bmin)
        .def_readwrite("bmax", &nw::MdlAABBEntry::bmax)
        .def_readwrite("leaf_face", &nw::MdlAABBEntry::leaf_face)
        .def_readwrite("plane", &nw::MdlAABBEntry::plane);
    // std::unique_ptr<MdlAABBEntry> left;
    // std::unique_ptr<MdlAABBEntry> right;

    py::class_<nw::MdlAABBNode, nw::MdlTrimeshNode>(nw, "MdlAABBNode")
        .def_readwrite("entries", &nw::MdlAABBNode::entries);

    // -- Geometry ----------------------------------------------------------------

    py::class_<nw::MdlGeometry>(nw, "MdlGeometry")
        .def_readwrite("name", &nw::MdlGeometry::name)
        .def_readwrite("type", &nw::MdlGeometry::type)
        .def("__len__", [](const nw::MdlGeometry& self) { return self.nodes.size(); })
        .def(
            "__getitem__", [](const nw::MdlGeometry& self, size_t index) {
                return self.nodes.at(index).get();
            },
            py::return_value_policy::reference)
        .def("nodes", [](const nw::MdlGeometry& self) {
            auto pylist = py::list();
            for (auto& ptr : self.nodes) {
                auto pyobj = py::cast(*ptr, py::return_value_policy::reference);
                pylist.append(pyobj);
            }
            return py::iter(pylist);
        });

    py::class_<nw::MdlAnimationEvent>(nw, "MdlAnimationEvent")
        .def_readwrite("time", &nw::MdlAnimationEvent::time)
        .def_readwrite("name", &nw::MdlAnimationEvent::name);

    py::class_<nw::MdlAnimation, nw::MdlGeometry>(nw, "MdlAnimation")
        .def_readwrite("length", &nw::MdlAnimation::length)
        .def_readwrite("transition_time", &nw::MdlAnimation::transition_time)
        .def_readwrite("anim_root", &nw::MdlAnimation::anim_root)
        .def_readonly("events", &nw::MdlAnimation::events);

    py::class_<nw::MdlModel, nw::MdlGeometry>(nw, "MdlModel")
        .def_readwrite("classification", &nw::MdlModel::classification)
        .def_readwrite("ignorefog", &nw::MdlModel::ignorefog)
        .def("animation_count", [](const nw::MdlModel& self) { return self.animations.size(); })
        .def(
            "get_animation", [](const nw::MdlModel& self, size_t index) {
                return self.animations.at(index).get();
            },
            py::return_value_policy::reference)
        .def("animations", [](const nw::MdlModel& self) {
            auto pylist = py::list();
            for (auto& ptr : self.animations) {
                auto pyobj = py::cast(*ptr, py::return_value_policy::reference);
                pylist.append(pyobj);
            }
            return py::iter(pylist);
        })
        .def_readwrite("supermodel", &nw::MdlModel::supermodel, py::return_value_policy::reference)
        .def_readwrite("bmin", &nw::MdlModel::bmin)
        .def_readwrite("bmax", &nw::MdlModel::bmax)
        .def_readwrite("radius", &nw::MdlModel::radius)
        .def_readwrite("animationscale", &nw::MdlModel::animationscale)
        .def_readwrite("supermodel_name", &nw::MdlModel::supermodel_name)
        .def_readwrite("file_dependency", &nw::MdlModel::file_dependency);

    py::class_<nw::Mdl>(nw, "Mdl")
        .def_static("from_file", [](const char* path) {
            auto mdl = new nw::Mdl{path};
            return mdl;
        })
        .def("valid", &nw::Mdl::valid)
        .def_property_readonly(
            "model", [](const nw::Mdl& self) {
                return &self.model;
            },
            py::return_value_policy::reference);
}
