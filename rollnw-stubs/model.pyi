import enum
from . import IVector4 as IVector4, Vector2 as Vector2, Vector3 as Vector3, Vector4 as Vector4
from typing import ClassVar, Iterator

class MdlNodeFlags:
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
    triangle = ...
    triangle_strip = ...

class MdlGeometryFlag(enum.IntEnum):
    geometry = ...
    model = ...
    animation = ...
    binary = ...

class MdlGeometryType(enum.IntEnum):
    geometry = ...
    model = ...
    animation = ...

class MdlClassification(enum.IntEnum):
    invalid = ...
    effect = ...
    tile = ...
    character = ...
    door = ...
    item = ...
    gui = ...

class MdlControllerType:
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
    name: str
    type: int
    rows: int
    key_offset: int
    time_offset: int
    data_offset: int
    columns: int
    is_key: bool

class MdlFace:
    vert_idx: list[int]
    shader_group_idx: int
    tvert_idx: list[int]
    material_idx: int

class MdlNode:
    name: str
    type: int
    inheritcolor: bool
    parent: MdlNode
    children: list['MdlNode']
    def get_controller(self, type: int, is_key: bool) -> tuple[MdlControllerKey, list[float], list[float]]: ...

class MdlDummyNode(MdlNode): ...
class MdlCameraNode(MdlNode): ...

class MdlEmitterNode(MdlNode):
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
    lensflares: float
    flareradius: float
    multiplier: float
    color: Vector3
    flaresizes: list[float]
    flarepositions: list[float]
    flarecolorshifts: list[Vector3]
    textures: list[str]
    lightpriority: int
    ambientonly: int
    dynamic: bool
    affectdynamic: int
    shadow: int
    generateflare: int
    fadinglight: int

class MdlPatchNode(MdlNode): ...

class MdlReferenceNode(MdlNode):
    refmodel: str
    reattachable: bool

class Vertex:
    position: Vector3
    tex_coords: Vector2
    normal: Vector3
    tangent: Vector3

class MdlTrimeshNode(MdlNode):
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
    textures: list[str]
    tilefade: int
    transparencyhint: int
    showdispl: bool
    displtype: int
    lightmapped: int
    multimaterial: list[str]
    vertices: list[Vertex]
    indices: list[int]

class SkinVertex:
    position: Vector3
    tex_coords: Vector2
    normal: Vector3
    tangent: Vector4
    bones: IVector4
    weights: Vector4

class MdlSkinNode(MdlTrimeshNode):
    vertices: list[SkinVertex]

class MdlAnimeshNode(MdlTrimeshNode):
    animtverts: list[Vector3]
    animverts: list[Vector3]
    sampleperiod: float

class MdlDanglymeshNode(MdlTrimeshNode):
    constraints: list[float]
    displacement: float
    period: float
    tightness: float

class MdlAABBEntry:
    bmin: Vector3
    bmax: Vector3
    leaf_face: int
    plane: int

class MdlAABBNode(MdlTrimeshNode):
    entries: list[MdlAABBEntry]

class MdlGeometry:
    name: str
    type: int
    def __getitem__(self, index) -> None: ...
    def __iter__(self): ...
    def __len__(self) -> int: ...

class MdlAnimationEvent:
    time: float
    name: str

class MdlAnimation(MdlGeometry):
    length: float
    transition_time: float
    anim_root: str
    events: list[MdlAnimationEvent]

class MdlModel(MdlGeometry):
    bmax: Vector3
    bmin: Vector3
    classification: int
    ignorefog: bool
    supermodel: Mdl | None
    radius: float
    animationscale: float
    supermodel_name: str
    file_dependency: str
    def animation_count(self) -> int: ...
    def animations(self) -> Iterator[MdlAnimation]: ...
    def get_animation(self, index: int) -> MdlAnimation: ...

class Mdl:
    model: MdlModel
    def valid(self) -> bool: ...
    @staticmethod
    def from_file(path) -> Mdl: ...
