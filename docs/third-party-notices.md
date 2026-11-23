# Third-party notices

aerial-lock's main license remains GPL-3.0-or-later. Media is not included or
relicensed by the software license; each source's media rights remain separate.

## Aerial

The Apple direct-download approach and README disclosure credit
[Aerial](https://github.com/AerialScreensaver/Aerial), originally started by John
Coates and maintained by Guillaume Louel and contributors. The factual adaptation
and attributed short quotation link to its
[legal page](https://aerialscreensaver.github.io/legal): "It does not host, mirror
or redistribute them." That website is not assumed to share the repository's
license. No wholesale website text or video is copied here.

Upstream repository software/documentation notice, from revision
`e6a81d1483afbe6e45ab3544e721c6de665f76ff` (copyright range rendered in ASCII):

```text
MIT License

Copyright (c) 2015-present Guillaume Louel, and contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Community sources remain explicit opt-ins. AerialCommunity's source-specific
media licenses are not replaced by Aerial's repository MIT license.

## InterActive

The settings app privately links the InterActive 0.1.1 shared library under
LGPL-3.0-or-later. Its library and QML files are installed under the app's
private `lib/aerial-lock/interactive/{lib,qml}` tree, not as a system module.
The vendored source builds offline with Qt SVG and ShaderTools.
Its complete signed-tag source, including the pinned Material Color Utilities
subset under Apache-2.0, is in `third_party/interactive`. See its `VENDOR.md`
for tag, commit, tree and archive identities, `LICENSE.md` for provenance, and
`COPYING` / `COPYING.LESSER` for license terms. The installed package includes
these texts and the MCU license/README under
`share/licenses/aerial-lock/interactive`. This does not relicense Aerial or media.
