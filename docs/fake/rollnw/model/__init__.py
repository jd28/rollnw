import enum
from enum import auto

# Model #######################################################################
###############################################################################


class MdlNodeFlags:
    """Model node flags

    Attributes:
        header
        light
        emitter
        camera
        reference
        mesh
        skin
        anim
        dangly
        aabb
        patch
    """
    pass


class MdlNodeType:
    """ Model node types

    Attributes:
        aabb
        animmesh
        camera
        danglymesh
        dummy
        emitter
        light
        patch
        reference
        skin
        trimesh
    """
    pass


class ModelEmitterFlag:
    """Emitter flags

    Attributes:
        affected_by_wind (int)
        bounce (int)
        inherit_local (int)
        inherit_part (int)
        inherit_vel (int)
        inherit (int)
        is_tinted (int)
        p2p_sel (int)
        p2p (int)
        random (int)
        splat (int)
    """
    pass


class MdlTriangleMode(enum.IntEnum):
    """Triangle mode
    """
    triangle = auto()
    triangle_strip = auto()


class MdlGeometryFlag(enum.IntEnum):
    """Geometry flags
    """
    geometry = auto()
    model = auto()
    animation = auto()
    binary = auto()


class MdlGeometryType(enum.IntEnum):
    """Geometry types
    """
    geometry = auto()
    model = auto()
    animation = auto()


class MdlClassification(enum.IntEnum):
    """Model classes
    """
    invalid = auto()
    effect = auto()
    tile = auto()
    character = auto()
    door = auto()
    item = auto()
    gui = auto()


class MdlControllerType:
    """Controller types

    Attributes:
        position
        orientation
        scale
        wirecolor
        color
        radius
        shadow_radius
        vertical_displacement
        multiplier
        alpha_end
        alpha_start
        birthrate
        bounce_co
        color_end
        color_start
        combine_time
        drag
        fps
        frame_end
        frame_start
        grav
        life_exp
        mass
        p2p_bezier2
        p2p_bezier3
        particle_rot
        rand_vel
        size_start
        size_end
        size_start_y
        size_end_y
        spread
        threshold
        velocity
        xsize
        ysize
        blur_length
        lightning_delay
        lightning_radius
        lightning_scale
        lightning_subdiv
        detonate
        alpha_mid
        color_mid
        percent_start
        percent_mid
        percent_end
        size_mid
        size_mid_y
        self_illum_color
        alpha
    """
    pass


class MdlControllerKey:
    """Model controller

    Attributes:
        name
        type
        rows
        key_offset
        data_offset
        columns
        is_key
    """
    pass


class MdlFace:
    """Model face

    Attributes:
        vert_idx
        shader_group_idx
        tvert_idx
        material_idx
    """
    pass


class MdlNode:
    """Base Model Node

    Attributes:
       name (str): name
       type: type
       inheritcolor (bool): inheritcolor
       parent (rollnw.MdlNode): Parent node
       children ([rollnw.MdlNode]): Children nodes
       controller_keys: controller_keys
       controller_data: controller_data
    """

    def get_controller(self, type: int, is_key: bool):
        """Gets a controller key and data
        """
        pass


class MdlDummyNode(MdlNode):
    """Dummy node"""
    pass


class MdlCameraNode(MdlNode):
    """Camera node"""
    pass


class MdlEmitterNode(MdlNode):
    """Emitter node

    Attributes:
        blastlength
        blastradius
        blend
        chunkname
        deadspace
        loop
        render
        renderorder
        spawntype
        texture
        twosidedtex
        update
        xgrid
        ygrid
        flags
        render_sel
        blend_sel
        update_sel
        spawntype_sel
        opacity
        p2p_type
    """
    pass


class MdlLightNode(MdlNode):
    """Light node

    Attributes:
        lensflares
        flareradius
        multiplier
        color
        flaresizes
        flarepositions
        flarecolorshifts
        textures
        lightpriority
        ambientonly
        dynamic
        affectdynamic
        shadow
        generateflare
        fadinglight
    """
    pass


class MdlPatchNode(MdlNode):
    """Patch node"""
    pass


class MdlReferenceNode(MdlNode):
    """Reference node

    Attributes:
        refmodel (str)
        reattachable (bool)
    """
    pass


class MdlTrimeshNode(MdlNode):
    """Trimesh Node

    Attributes:
        ambient
        beaming
        bmin
        bmax
        bitmap
        center
        diffuse
        faces
        materialname
        render
        renderhint
        rotatetexture
        shadow
        shininess
        specular
        textures
        tilefade
        transparencyhint
        showdispl
        displtype
        lightmapped
        multimaterial
        colors
        verts
        tverts
        normals
        tangents
    """
    pass


class MdlSkinWeight:
    """Weights for skin meshes

    Attributes:
        bones
        weights
    """
    pass


class MdlSkinNode(MdlTrimeshNode):
    """Skin mesh node

    Attributes:
        weights ([MdlSkinWeight])
    """
    pass


class MdlAnimeshNode(MdlTrimeshNode):
    """Animated mesh node

    Attributes:
        animtverts
        animverts
        sampleperiod
    """
    pass


class MdlDanglymeshNode(MdlTrimeshNode):
    """MdlDanglymeshNode

    Attributes:
        constraints
        displacement
        period
        tightness
    """
    pass


class MdlAABBEntry:
    """AABB Entry

    Attributes:
        bmin
        bmax
        leaf_face
        plane
    """
    pass


class MdlAABBNode(MdlTrimeshNode):
    """AABB model node

    Attributes:
        entries
    """
    pass


class MdlGeometry:
    """Class containing model geometry

    Attributes:
       name (str): name
       type: type
    """

    def __getitem__(self, index):
        """Gets a model node"""
        pass

    def __iter__(self):
        """Gets an iterator of model nodes"""
        pass

    def __len__(self):
        """Gets number of model nodes"""
        pass


class MdlAnimationEvent:
    """Animation Event

    Attributes:
        time (float)
        name (str)
    """
    pass


class MdlAnimation(MdlGeometry):
    """Class containing model animation

    Attributes:
        length
        transition_time
        anim_root
        events
    """
    pass


class MdlModel(MdlGeometry):
    """A parsed model

    Attributes:
        bmax (vec3): bmax
        bmin (vec3): bmin
        classification: classification
        ignorefog: ignorefog
        supermodel: supermodel
        radius: radius
        animationscale: animationscale
        supermodel_name (str): supermodel_name
        file_dependency: file_dependency
    """

    def animation_count(self):
        """Gets the number of animations"""
        pass

    def animations(self):
        """Gets an iterator of animations"""
        pass

    def get_animation(self, index: int):
        """Gets an animation"""
        pass


class Mdl:
    """Implementation of ASCII Mdl file format

    Attributes:
        model (rollnw.MdlModel): The parsed model
    """

    def valid():
        """Determines if file was successfully parsed"""
        pass

    @staticmethod
    def from_file(path):
        """Loads mdl file from file path"""
        pass

    pass
