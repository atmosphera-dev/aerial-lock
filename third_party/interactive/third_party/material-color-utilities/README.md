# Material Color Utilities (Private Subset)

Upstream: <https://github.com/material-foundation/material-color-utilities>

Pinned commit: `5b3618b16fdc3825e21d5679bafd144662088ea1`

Copyright Google LLC. The upstream source files retain their per-file copyright
notices; the verbatim [LICENSE](LICENSE) retains its 2021 copyright notice.
This subset is licensed under Apache-2.0, not InterActive's LGPL license.

## Scope

The original `cpp/` layout is preserved. Included files are `cam/hct.cc`,
`cam/hct.h`, `cam/hct_solver.cc`, `cam/hct_solver.h`, `cam/cam.cc`, `cam/cam.h`,
`cam/viewing_conditions.h`, `utils/utils.cc`, `utils/utils.h`,
`contrast/contrast.cc`, and `contrast/contrast.h`.

These provide HCT, CAM16, the HCT solver, contrast, and their utility dependency
closure. They compile directly into `interactive_foundation` as private sources
with a private include root. No vendor headers are exposed through the target's
include interface, and no separate vendor link or runtime dependency is added.
The Material theme engine and other upstream modules are not included.

`viewing_conditions.h` supplies the default viewing-condition constants used by
this subset. Its unchanged `CreateViewingConditions` and
`DefaultWithBackgroundLstar` declarations are outside this subset: their
implementation in `viewing_conditions.cc` is neither needed by HCT nor included.

## Local Adaptation

Only `cpp/utils/utils.cc` differs from the pinned upstream source. It carries a
modification notice, drops the Abseil include, and replaces `HexFromArgb`'s
`absl::StrCat(absl::Hex(argb))` with `std::snprintf` using `%lx`, an
`unsigned long` cast, and `char buffer[9]`. Output remains lowercase, unpadded
hexadecimal for the entire 32-bit ARGB value, including alpha (for example,
`0` becomes `"0"` and `0xff012345` becomes `"ff012345"`). No color mathematics,
constants, other source files, headers, or upstream license notices are changed.
