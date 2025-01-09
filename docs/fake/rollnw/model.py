import enum
from enum import auto
from typing import Tuple, List, ClassVar, Optional, Iterator

from . import (
    IVector4,
    Vector2,
    Vector3,
    Vector4,
)

# Model #######################################################################
###############################################################################


class MdlNodeFlags:
    """Model node flags
    """
    header: ClassVar[int]
    light: ClassVar[int]
    emitter: ClassVar[int]
    camera: ClassVar[int]
    reference: ClassVar[int]
    mesh: ClassVar[int]
    skin: ClassVar[int]
    anim: ClassVar[int]
    dangly: ClassVar[int]
    aabb: ClassVar[int]
    patch: ClassVar[int]


class MdlNodeType:
    """ Model node types
    """
    aabb: ClassVar[int]
    animmesh: ClassVar[int]
    camera: ClassVar[int]
    danglymesh: ClassVar[int]
    dummy: ClassVar[int]
    emitter: ClassVar[int]
    light: ClassVar[int]
    patch: ClassVar[int]
    reference: ClassVar[int]
    skin: ClassVar[int]
    trimesh: ClassVar[int]


class ModelEmitterFlag:
    """Emitter flags
    """
    affected_by_wind: ClassVar[int]
    bounce: ClassVar[int]
    inherit_local: ClassVar[int]
    inherit_part: ClassVar[int]
    inherit_vel: ClassVar[int]
    inherit: ClassVar[int]
    is_tinted: ClassVar[int]
    p2p_sel: ClassVar[int]
    p2p: ClassVar[int]
    random: ClassVar[int]
    splat: ClassVar[int]


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
    """
    position: ClassVar[int]
    orientation: ClassVar[int]
    scale: ClassVar[int]
    wirecolor: ClassVar[int]
    color: ClassVar[int]
    radius: ClassVar[int]
    shadow_radius: ClassVar[int]
    vertical_displacement: ClassVar[int]
    multiplier: ClassVar[int]
    alpha_end: ClassVar[int]
    alpha_start: ClassVar[int]
    birthrate: ClassVar[int]
    bounce_co: ClassVar[int]
    color_end: ClassVar[int]
    color_start: ClassVar[int]
    combine_time: ClassVar[int]
    drag: ClassVar[int]
    fps: ClassVar[int]
    frame_end: ClassVar[int]
    frame_start: ClassVar[int]
    grav: ClassVar[int]
    life_exp: ClassVar[int]
    mass: ClassVar[int]
    p2p_bezier2: ClassVar[int]
    p2p_bezier3: ClassVar[int]
    particle_rot: ClassVar[int]
    rand_vel: ClassVar[int]
    size_start: ClassVar[int]
    size_end: ClassVar[int]
    size_start_y: ClassVar[int]
    size_end_y: ClassVar[int]
    spread: ClassVar[int]
    threshold: ClassVar[int]
    velocity: ClassVar[int]
    xsize: ClassVar[int]
    ysize: ClassVar[int]
    blur_length: ClassVar[int]
    lightning_delay: ClassVar[int]
    lightning_radius: ClassVar[int]
    lightning_scale: ClassVar[int]
    lightning_subdiv: ClassVar[int]
    detonate: ClassVar[int]
    alpha_mid: ClassVar[int]
    color_mid: ClassVar[int]
    percent_start: ClassVar[int]
    percent_mid: ClassVar[int]
    percent_end: ClassVar[int]
    size_mid: ClassVar[int]
    size_mid_y: ClassVar[int]
    self_illum_color: ClassVar[int]
    alpha: ClassVar[int]


class MdlControllerKey:
    """Model controller
    """
    name: str
    type: int
    rows: int
    key_offset: int
    time_offset: int
    data_offset: int
    columns: int
    is_key: bool


class MdlFace:
    """Model face
    """
    vert_idx: List[int]
    shader_group_idx: int
    tvert_idx: List[int]
    material_idx: int


class MdlNode:
    """Base Model Node
    """
    name: str
    type: int
    inheritcolor: bool
    parent: "MdlNode"
    children: List["MdlNode"]

    def get_controller(self, type: int, is_key: bool) -> Tuple[MdlControllerKey, List[float], List[float]]:
        """Gets a controller key and times and key data

        Note:
            If not an animation, time will be empty
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
    """
    blastlength: float
    blastradius: float
    blend: str
    chunkname: str
    deadspace: float
    loop: int
    render: str
    renderorder: int
    spawntype: int
    texture: str
    twosidedtex: int
    update: str
    xgrid: int
    ygrid: int
    flags: int
    render_sel: int
    blend_sel: int
    update_sel: int
    spawntype_sel: int
    opacity: float
    p2p_type: str


class MdlLightNode(MdlNode):
    """Light node
    """
    lensflares: float
    flareradius: float
    multiplier: float
    color: Vector3
    flaresizes: List[float]
    flarepositions: List[float]
    flarecolorshifts: List[Vector3]
    textures: List[str]
    lightpriority: int
    ambientonly: int
    dynamic: bool
    affectdynamic: int
    shadow: int
    generateflare: int
    fadinglight: int


class MdlPatchNode(MdlNode):
    """Patch node"""
    pass


class MdlReferenceNode(MdlNode):
    """Reference node
    """
    refmodel: str
    reattachable: bool


class Vertex:
    """Vertex data
    """
    position: Vector3
    tex_coords: Vector2
    normal: Vector3
    tangent: Vector3


class MdlTrimeshNode(MdlNode):
    """Trimesh Node
    """
    ambient: Vector3
    beaming: bool
    bmin: Vector3
    bmax: Vector3
    bitmap: str
    center: Vector3
    diffuse: Vector3
    materialname: str
    render: bool
    renderhint: str
    rotatetexture: bool
    shadow: bool
    shininess: float
    specular: Vector3
    textures: List[str]
    tilefade: int
    transparencyhint: int
    showdispl: bool
    displtype: int
    lightmapped: int
    multimaterial: List[str]
    #: List of vertex positions, texcoords, normals, tangents
    vertices: List[Vertex]
    #: List of vertex indices
    indices: List[int]


class SkinVertex:
    """Skin Vertex data
    """
    position: Vector3
    tex_coords: Vector2
    normal: Vector3
    tangent: Vector4
    bones: IVector4
    weights: Vector4


class MdlSkinNode(MdlTrimeshNode):
    """Skin mesh node
    """
    vertices: List[SkinVertex]


class MdlAnimeshNode(MdlTrimeshNode):
    """Animated mesh node
    """
    animtverts: List[Vector3]
    animverts: List[Vector3]
    sampleperiod: float


class MdlDanglymeshNode(MdlTrimeshNode):
    """MdlDanglymeshNode
    """
    constraints: List[float]
    displacement: float
    period: float
    tightness: float


class MdlAABBEntry:
    """AABB Entry
    """
    bmin: Vector3
    bmax: Vector3
    leaf_face: int
    plane: int


class MdlAABBNode(MdlTrimeshNode):
    """AABB model node
    """
    entries: List[MdlAABBEntry]


class MdlGeometry:
    """Class containing model geometry
    """
    name: str
    type: int

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
    """
    time: float
    name: str


class MdlAnimation(MdlGeometry):
    """Class containing model animation
    """
    length: float
    transition_time: float
    anim_root: str
    events: List[MdlAnimationEvent]


class MdlModel(MdlGeometry):
    """A parsed model
    """
    bmax: Vector3
    bmin: Vector3
    classification: int
    ignorefog: bool
    supermodel: "Optional[Mdl]"
    radius: float
    animationscale: float
    supermodel_name: str
    file_dependency: str

    def animation_count(self) -> int:
        """Gets the number of animations"""
        pass

    def animations(self) -> Iterator[MdlAnimation]:
        """Gets an iterator of animations"""
        pass

    def get_animation(self, index: int) -> MdlAnimation:
        """Gets an animation"""
        pass


class Mdl:
    """Implementation of ASCII Mdl file format
    """
    #: The parsed model
    model: MdlModel

    def valid(self) -> bool:
        """Determines if file was successfully parsed"""
        pass

    @staticmethod
    def from_file(path) -> 'Mdl':
        """Loads mdl file from file path"""
        pass
