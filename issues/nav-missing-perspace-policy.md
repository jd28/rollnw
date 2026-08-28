# Missing creature PERSPACE navigation policy

Status: unresolved input policy; blocks complete radius-class registration.

The 2026-08-27 appearance audit observed 15,100 rows, only 838 finite
`PERSPACE` values, and 14,262 missing values. The measured values range from
0.01 m to 6.0 m. This is not evidence that every missing row has radius zero,
and resource or model names must not be used to infer a radius.

An already-eroded `NavWorldState` can reject an actor whose radius class is not
the world's class, but it still needs a real radius before classification. Find
the actual runtime fallback used by NWN appearance resolution, or collect the
resolved spatial radius after creature instantiation. Until then, the Recast
candidate may audit known rows and the selected F9 actor only; it must not
register unknown game-mode actors into a fabricated class.
