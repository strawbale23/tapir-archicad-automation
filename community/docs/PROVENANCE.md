# Provenance and compatibility

Updated: 19 September 2026, 14:06 CEST (Europe/Berlin).

This source snapshot is derived from Tapir, developed by ENZYME APD, and subsequent local experimental changes. The audit target was upstream 1.5.8. The starting local checkout also contained earlier experiments; this is not presented as a minimal patch against a pristine 1.5.8 tag.

A reproducible comparison now exists against original revision
`ce033d6bdcc90b538b3c5f7ab62f676099b96823`: 73 native source/header/JSON files were
matched to their saved GitHub tree blob hashes. See [UPSTREAM_COMPARISON.md](UPSTREAM_COMPARISON.md)
and its JSON companion for added registrations and changed files/input contracts.
This does not establish who authored every intermediate change or full SDK coverage.

The private working repository's history is intentionally not imported. The sharing repository starts with a clean snapshot, retaining the original licence and attribution. `SOURCE_MANIFEST.json` identifies every delivered payload file by SHA-256. This makes the snapshot identifiable without exposing private history.

For a future contribution to upstream, reconcile the changes with a known upstream revision on a dedicated branch and submit focused pull requests. Publishing this independent repository does not create a GitHub fork relationship or send a pull request to Tapir automatically.

## Compatibility changes requiring review

- Some previously accepted ineffective or ambiguous inputs now return errors. Runtime schemas are authoritative for the installed development build.
- Composite indices are integers; unknown skin/use types and invalid resource references are rejected. New composites require explicit usage. `ifExists` is mutually exclusive with the legacy overwrite flag.
- Layout creation requires exactly one master selector. Paper dimensions/margins belong to master layouts; ordinary sheets reject those inputs. Explicit reuse policies are opt-in.
- Column top-story links are integers; explicit height and top-story links cannot be supplied together. Conflicting section selectors are rejected.
- Rectangular beam holes require height; circular holes omit it. Empty hole replacement is implemented with a null memo and still needs native acceptance.
- New response metadata may be present. Existing result arrays remain available, but clients that reject all unknown response fields need compatibility testing.

The development version remains `1.5.8-ai.4`; use the source manifest, binary hash and runtime contracts to distinguish experimental builds. Do not claim upstream endorsement or a supported production release.

In ai.4, all attribute overwrite indices are integers. Object/lamp names must resolve uniquely; explicit index/GUID selection is supported. Missing supported object height parameters now fail. Partial morph creation may return elementId and error together with status=createdWithError; clients must inspect both. See [ai.4 command notes](NATIVE_AI4_COMMANDS.md) for receipt semantics and further compatibility changes.
