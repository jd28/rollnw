# Equipment proficiency alternatives

Status: resolved and verified, 2026-10-24.

`nwn1.baseitems.requirements_met` previously required every imported `ReqFeat0..4` row to
match. The short-sword row in `tests/test_data/user/development/baseitems.2da`
contains feats 45 and 50. `pl_agent_001.utc.json` has 45 and lacks 50, and the
gameplay equip path rejected `nw_wswss001` in the right hand.

The rule now accepts any matching proficiency; an item with no requirements
remains unrestricted. Slot eligibility remains a separate check.
`SmallsEngineIntegration.Nwn1BaseItemRequirementsUseSmallsRules` covers individual
proficiencies, multiple proficiencies, neither, an unrestricted item and invalid
base-item data, including all five alternatives for the quarterstaff.

The preview calls the gameplay rules; it does not bypass this check.
Its visual acceptance test now equips a quarterstaff through inventory dragging, while
the headless transfer/equipment test uses a belt and verifies its effects.
