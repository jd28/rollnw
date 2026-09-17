# RmlUi scrollbar destruction lifetime

Status: closed on 2026-09-17; repaired and verified with the independent regression.

The C14 popup fixture exposed an ASan heap-use-after-free in vendored RmlUi
6.2: Element destroys its children before ElementMeta destroys ElementScroll.
WidgetScroll then removes listeners from its already-destroyed slider children.
The failure occurs when replacing a laid-out overflowing element.

Tier 1 plan: reproduce with a plain Rml document and both scrollbars, independent
of client providers. Release the two existing scrollbar widgets before child
destruction, retaining child/plugin notifications and metadata destruction order.
Input is one SDK element's owned children and up to two scrollbar widgets;
output is destruction with listeners detached while their elements remain live.
These are true singleton lifetime operations, not batch data transformations.
Cost is two bounded resets at element destruction, no new state or allocations.
Simplification reuses the existing widget destructors rather than changing DOM
ownership or deferring deletion. Done requires the independent regression to fail
under ASan before the repair and pass afterward, plus the C14 popup cases.
No performance improvement or desktop scrollbar behavior is claimed.

The independent regression failed under ASan before the repair. Element now
releases its scrollbar widgets before clearing children; widget destructors
remove listeners while the slider/track/arrow elements are alive. The SDK's
existing child/plugin notifications and final metadata destruction remain in
order. No owning pointer or new cleanup state was added.

Normal and combined ASan/UBSan client/test builds passed. The 51-case creature,
workbench, template, managed-list and independent-scrollbar matrix passed:
11,176 ms normal and 62,408 ms sanitized, no skips. Both vertical/horizontal
scrollbars, document replacement and closing were exercised. The normal SDK
rebuild emitted nine GCC warnings from unchanged PropertyParserColour.cpp;
no new warning came from the repair or moved client sources. Desktop scrollbar
feel was not tested; the previously documented baseline VM LSan exclusion applies.
Simplification/self-check: two bounded widget resets reuse existing destruction,
no deferred-delete queue or DOM ownership rewrite; framing, lifetime and cost
contracts were checked against the actual failure and regression.
