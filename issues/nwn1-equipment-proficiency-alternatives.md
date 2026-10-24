# Confirm equipment proficiency requirement aggregation

Observed while testing preview item interactions, 2026-10-24.

`nwn1.baseitems.requirements_met` requires every imported `ReqFeat0..4` row to
match. The short-sword row in `tests/test_data/user/development/baseitems.2da`
contains feats 45 and 50. `pl_agent_001.utc.json` has 45 and lacks 50, and the
current gameplay equip path rejects `nw_wswss001` in the right hand.
`SmallsEngineIntegration.Nwn1BaseItemRequirementsUseSmallsRules` currently checks
no feats and both feats, leaving the one-proficiency case uncovered.

Open question: should these columns be alternatives or cumulative requirements?
Confirm against the original game's behavior or its data-format contract, then
add cases for each individual proficiency, both, neither, and an unrestricted
base item before changing the rule. Keep slot eligibility separate.

The preview calls the existing gameplay rules; it does not bypass this check.
Its visual acceptance test uses a battleaxe with a single requirement, while
the headless transfer/equipment test uses a belt and verifies its effects.
