from typing import Any, ClassVar, Iterator, List

import rollnw

class Mdl:
    def __init__(self, *args, **kwargs) -> None: ...
    def from_file(self, *args, **kwargs) -> Any: ...
    def valid(self) -> bool: ...
    @property
    def model(self) -> MdlModel: ...

class MdlAABBEntry:
    bmax: rollnw.Vector3
    bmin: rollnw.Vector3
    leaf_face: int
    plane: int
    def __init__(self, *args, **kwargs) -> None: ...

class MdlAABBNode(MdlTrimeshNode):
    entries: List[MdlAABBEntry]
    def __init__(self, *args, **kwargs) -> None: ...

class MdlAnimation(MdlGeometry):
    anim_root: str
    length: float
    transition_time: float
    def __init__(self, *args, **kwargs) -> None: ...
    @property
    def events(self) -> List[MdlAnimationEvent]: ...

class MdlAnimationEvent:
    name: str
    time: float
    def __init__(self, *args, **kwargs) -> None: ...

class MdlAnimeshNode(MdlTrimeshNode):
    animtverts: rollnw.Vec3Vector
    animverts: rollnw.Vec3Vector
    sampleperiod: float
    def __init__(self, *args, **kwargs) -> None: ...

class MdlCameraNode(MdlNode):
    def __init__(self, *args, **kwargs) -> None: ...

class MdlClassification:
    __members__: ClassVar[dict] = ...  # read-only
    __entries: ClassVar[dict] = ...
    character: ClassVar[MdlClassification] = ...
    door: ClassVar[MdlClassification] = ...
    effect: ClassVar[MdlClassification] = ...
    gui: ClassVar[MdlClassification] = ...
    invalid: ClassVar[MdlClassification] = ...
    item: ClassVar[MdlClassification] = ...
    tile: ClassVar[MdlClassification] = ...
    def __init__(self, value: int) -> None: ...
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __int__(self) -> int: ...
    def __ne__(self, other: object) -> bool: ...
    def __setstate__(self, state: int) -> None: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class MdlControllerKey>:
    columns: int
    data_offset: int
    is_key: bool
    key_offset: int
    name: InternedString
    rows: int
    type: int
    def __init__(self, *args, **kwargs) -> None: ...

class MdlControllerType:
    alpha: ClassVar[int] = ...  # read-only
    alpha_end: ClassVar[int] = ...  # read-only
    alpha_mid: ClassVar[int] = ...  # read-only
    alpha_start: ClassVar[int] = ...  # read-only
    birthrate: ClassVar[int] = ...  # read-only
    blur_length: ClassVar[int] = ...  # read-only
    bounce_co: ClassVar[int] = ...  # read-only
    color: ClassVar[int] = ...  # read-only
    color_end: ClassVar[int] = ...  # read-only
    color_mid: ClassVar[int] = ...  # read-only
    color_start: ClassVar[int] = ...  # read-only
    combine_time: ClassVar[int] = ...  # read-only
    detonate: ClassVar[int] = ...  # read-only
    drag: ClassVar[int] = ...  # read-only
    fps: ClassVar[int] = ...  # read-only
    frame_end: ClassVar[int] = ...  # read-only
    frame_start: ClassVar[int] = ...  # read-only
    grav: ClassVar[int] = ...  # read-only
    life_exp: ClassVar[int] = ...  # read-only
    lightning_delay: ClassVar[int] = ...  # read-only
    lightning_radius: ClassVar[int] = ...  # read-only
    lightning_scale: ClassVar[int] = ...  # read-only
    lightning_subdiv: ClassVar[int] = ...  # read-only
    mass: ClassVar[int] = ...  # read-only
    multiplier: ClassVar[int] = ...  # read-only
    orientation: ClassVar[int] = ...  # read-only
    p2p_bezier2: ClassVar[int] = ...  # read-only
    p2p_bezier3: ClassVar[int] = ...  # read-only
    particle_rot: ClassVar[int] = ...  # read-only
    percent_end: ClassVar[int] = ...  # read-only
    percent_mid: ClassVar[int] = ...  # read-only
    percent_start: ClassVar[int] = ...  # read-only
    position: ClassVar[int] = ...  # read-only
    radius: ClassVar[int] = ...  # read-only
    rand_vel: ClassVar[int] = ...  # read-only
    scale: ClassVar[int] = ...  # read-only
    self_illum_color: ClassVar[int] = ...  # read-only
    shadow_radius: ClassVar[int] = ...  # read-only
    size_end: ClassVar[int] = ...  # read-only
    size_end_y: ClassVar[int] = ...  # read-only
    size_mid: ClassVar[int] = ...  # read-only
    size_mid_y: ClassVar[int] = ...  # read-only
    size_start: ClassVar[int] = ...  # read-only
    size_start_y: ClassVar[int] = ...  # read-only
    spread: ClassVar[int] = ...  # read-only
    threshold: ClassVar[int] = ...  # read-only
    velocity: ClassVar[int] = ...  # read-only
    vertical_displacement: ClassVar[int] = ...  # read-only
    wirecolor: ClassVar[int] = ...  # read-only
    xsize: ClassVar[int] = ...  # read-only
    ysize: ClassVar[int] = ...  # read-only
    def __init__(self, *args, **kwargs) -> None: ...

class MdlDanglymeshNode(MdlTrimeshNode):
    constraints: rollnw.UInt32Vector
    displacement: float
    period: float
    tightness: float
    def __init__(self, *args, **kwargs) -> None: ...

class MdlDummyNode(MdlNode):
    def __init__(self, *args, **kwargs) -> None: ...

class MdlEmitterFlag:
    affected_by_wind: ClassVar[int] = ...  # read-only
    bounce: ClassVar[int] = ...  # read-only
    inherit: ClassVar[int] = ...  # read-only
    inherit_local: ClassVar[int] = ...  # read-only
    inherit_part: ClassVar[int] = ...  # read-only
    inherit_vel: ClassVar[int] = ...  # read-only
    is_tinted: ClassVar[int] = ...  # read-only
    p2p: ClassVar[int] = ...  # read-only
    p2p_sel: ClassVar[int] = ...  # read-only
    random: ClassVar[int] = ...  # read-only
    splat: ClassVar[int] = ...  # read-only
    def __init__(self, *args, **kwargs) -> None: ...

class MdlEmitterNode(MdlNode):
    blastlength: float
    blastradius: float
    blend: str
    blend_sel: int
    chunkname: str
    deadspace: float
    flags: int
    loop: int
    opacity: float
    p2p_type: str
    render: str
    render_sel: int
    renderorder: int
    spawntype: int
    spawntype_sel: int
    texture: str
    twosidedtex: int
    update: str
    update_sel: int
    xgrid: int
    ygrid: int
    def __init__(self, *args, **kwargs) -> None: ...

class MdlFace:
    material_idx: int
    shader_group_idx: int
    tvert_idx: List[int[3]]
    vert_idx: List[int[3]]
    def __init__(self, *args, **kwargs) -> None: ...

class MdlGeometry:
    name: str
    type: MdlGeometryType
    def __init__(self, *args, **kwargs) -> None: ...
    def nodes(self) -> Iterator: ...
    def __getitem__(self, arg0: int) -> MdlNode: ...
    def __len__(self) -> int: ...

class MdlGeometryFlag:
    __members__: ClassVar[dict] = ...  # read-only
    __entries: ClassVar[dict] = ...
    animation: ClassVar[MdlGeometryFlag] = ...
    binary: ClassVar[MdlGeometryFlag] = ...
    geometry: ClassVar[MdlGeometryFlag] = ...
    model: ClassVar[MdlGeometryFlag] = ...
    def __init__(self, value: int) -> None: ...
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __int__(self) -> int: ...
    def __ne__(self, other: object) -> bool: ...
    def __setstate__(self, state: int) -> None: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class MdlGeometryType:
    __members__: ClassVar[dict] = ...  # read-only
    __entries: ClassVar[dict] = ...
    animation: ClassVar[MdlGeometryType] = ...
    geometry: ClassVar[MdlGeometryType] = ...
    model: ClassVar[MdlGeometryType] = ...
    def __init__(self, value: int) -> None: ...
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __int__(self) -> int: ...
    def __ne__(self, other: object) -> bool: ...
    def __setstate__(self, state: int) -> None: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class MdlLightNode(MdlNode):
    affectdynamic: int
    ambientonly: int
    color: rollnw.Vector3
    dynamic: bool
    fadinglight: int
    flarecolorshifts: rollnw.Vec3Vector
    flarepositions: List[float]
    flareradius: float
    flaresizes: List[float]
    generateflare: int
    lightpriority: int
    multiplier: float
    shadow: int
    textures: rollnw.StringVector
    def __init__(self, *args, **kwargs) -> None: ...

class MdlModel(MdlGeometry):
    animationscale: float
    bmax: rollnw.Vector3
    bmin: rollnw.Vector3
    classification: MdlClassification
    file_dependency: str
    ignorefog: bool
    radius: float
    supermodel_name: str
    def __init__(self, *args, **kwargs) -> None: ...
    def animation_count(self) -> int: ...
    def animations(self) -> Iterator: ...
    def get_animation(self, arg0: int) -> MdlAnimation: ...
    @property
    def supermodel(self) -> Any: ...

class MdlNode:
    def __init__(self, *args, **kwargs) -> None: ...
    def get_controller(self, arg0: int, arg1: bool) -> tuple: ...
    @property
    def children(self) -> List[MdlNode]: ...
    @property
    def controller_data(self) -> List[float]: ...
    @property
    def controller_keys(self) -> Any: ...
    @property
    def inheritcolor(self) -> bool: ...
    @property
    def name(self) -> str: ...
    @property
    def parent(self) -> MdlNode: ...
    @property
    def type(self) -> int: ...

class MdlNodeFlags:
    aabb: ClassVar[int] = ...  # read-only
    anim: ClassVar[int] = ...  # read-only
    camera: ClassVar[int] = ...  # read-only
    dangly: ClassVar[int] = ...  # read-only
    emitter: ClassVar[int] = ...  # read-only
    header: ClassVar[int] = ...  # read-only
    light: ClassVar[int] = ...  # read-only
    mesh: ClassVar[int] = ...  # read-only
    patch: ClassVar[int] = ...  # read-only
    reference: ClassVar[int] = ...  # read-only
    skin: ClassVar[int] = ...  # read-only
    def __init__(self, *args, **kwargs) -> None: ...

class MdlNodeType:
    aabb: ClassVar[int] = ...  # read-only
    animmesh: ClassVar[int] = ...  # read-only
    camera: ClassVar[int] = ...  # read-only
    danglymesh: ClassVar[int] = ...  # read-only
    dummy: ClassVar[int] = ...  # read-only
    emitter: ClassVar[int] = ...  # read-only
    light: ClassVar[int] = ...  # read-only
    patch: ClassVar[int] = ...  # read-only
    reference: ClassVar[int] = ...  # read-only
    skin: ClassVar[int] = ...  # read-only
    trimesh: ClassVar[int] = ...  # read-only
    def __init__(self, *args, **kwargs) -> None: ...

class MdlPatchNode(MdlNode):
    def __init__(self, *args, **kwargs) -> None: ...

class MdlReferenceNode(MdlNode):
    reattachable: bool
    refmodel: str
    def __init__(self, *args, **kwargs) -> None: ...

class MdlSkinNode(MdlTrimeshNode):
    weights: List[MdlSkinWeight]
    def __init__(self, *args, **kwargs) -> None: ...

class MdlSkinWeight:
    bones: List[str[4]]
    weights: List[float[4]]
    def __init__(self, *args, **kwargs) -> None: ...

class MdlTriangleMode:
    __members__: ClassVar[dict] = ...  # read-only
    __entries: ClassVar[dict] = ...
    triangle: ClassVar[MdlTriangleMode] = ...
    triangle_strip: ClassVar[MdlTriangleMode] = ...
    def __init__(self, value: int) -> None: ...
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __int__(self) -> int: ...
    def __ne__(self, other: object) -> bool: ...
    def __setstate__(self, state: int) -> None: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class MdlTrimeshNode(MdlNode):
    ambient: rollnw.Vector3
    beaming: bool
    bitmap: str
    bmax: rollnw.Vector3
    bmin: rollnw.Vector3
    center: rollnw.Vector3
    colors: rollnw.Vec3Vector
    diffuse: rollnw.Vector3
    displtype: int
    lightmapped: int
    materialname: str
    multimaterial: rollnw.StringVector
    render: bool
    renderhint: str
    rotatetexture: bool
    shadow: bool
    shininess: float
    showdispl: bool
    specular: rollnw.Vector3
    textures: List[str[3]]
    tilefade: int
    transparencyhint: int
    vertices: rollnw.VertexVector
    def __init__(self, *args, **kwargs) -> None: ...

class Vertex:
    normal: rollnw.Vector3
    position: rollnw.Vector3
    tangent: rollnw.Vector4
    tex_coords: rollnw.Vector2
    def __init__(self, *args, **kwargs) -> None: ...
