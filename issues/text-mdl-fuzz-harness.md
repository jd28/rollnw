# Text MDL Fuzz Harness

## Data

Input is hostile text `.mdl` bytes. Unlike binary MDL parsing, the text MDL
path can intern model names and resolve supermodels through kernel strings and
resources, so it is not a pure bytes-in/parser-out transform.

The common malformed case is a short or partially valid text model. The desired
output is either a valid parsed model or a clean invalid model, with no service
lifetime leaks and no sanitizer findings.

## Work

- Build a text-MDL-specific fuzz harness with an explicit kernel service
  lifetime contract.
- Keep raw format parser fuzzing separate from service-backed text MDL fuzzing.
- Run under ASan+UBSan with a small seed corpus of valid and malformed text MDL
  snippets.
- Close this only after the harness can run in CI without depending on static
  process teardown.

## Notes

The memory hardening issue closes the raw parser and allocator/container
assurance work. Text MDL remains separate because its real boundary is
resource/string-service-backed model loading, not just file bytes.
