# nw::gfx

`nw::gfx` is a thin rendering abstraction and Vulkan utility layer.

It is not a game engine, scene graph, material system, or editor framework.

## Project Status

`nw::gfx` is the active low-level graphics foundation for rollNW renderer work and future authored-toolset/game viewports. Higher-level validation currently lives in `lib/nw/render` and `tools/mudl`; older project docs may still describe rollNW as if graphics were out of scope. Read those older statements as historical context, not current direction.

## What It Is For

- Creating devices and swapchains
- Managing GPU resources such as buffers, textures, render targets, shaders, and pipelines
- Recording and submitting command lists
- Exposing low-level rendering primitives with explicit ownership and lifetimes
- Providing backend escape hatches when higher layers need direct Vulkan access

## What It Is Not For

- Scene graphs, entity systems, gameplay logic, or runtime policy
- Asset pipeline decisions, editor state, or tool-specific UI behavior
- Material databases or content authoring abstractions
- Hiding lifetimes behind implicit global behavior
- Turning backend helpers into a framework with engine-level policy

## Design Principles

- Thin layer first. Keep APIs close to the underlying GPU model.
- Explicit ownership. Resources, command submission, and synchronization should stay easy to reason about.
- Low ceremony. Prefer small descriptors and direct calls over large abstraction stacks.
- Validation-clean behavior. Favor correctness around resize, present/acquire, resource transitions, and shutdown.
- Escape hatches matter. Callers should still be able to reach raw backend handles when they need to.
- Bindless-first direction. Prefer resource models that scale to many textures and draw calls without per-draw descriptor churn.

## Bindless Direction

`nw::gfx` is moving toward a bindless-first resource model.

Current direction:

- descriptor-buffer-backed Vulkan backend
- bindless sampled texture indexing
- per-frame uniform suballocation for per-draw constants
- pipeline flags that opt into resource models instead of hard-coding one path

This does not mean every resource class is fully bindless yet, but it does mean new work should avoid doubling down on one-resource-per-draw designs when a scalable bindless path is the better fit.

## Review Checklist

- Does this strengthen `nw::gfx` as the long-term foundation for the renderer without pulling higher-level game policy into it?
- Does this avoid encoding product, editor, or gameplay policy?
- Does this keep ownership and lifetime explicit?
- Does this preserve debuggability and low overhead?
- Does this move the API toward scalable resource binding instead of away from it?
- Can callers still reach raw backend handles when required?

If a change fails these checks, it likely belongs outside `lib/nw/gfx`.
