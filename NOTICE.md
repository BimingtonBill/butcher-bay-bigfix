# Credits and third-party notices

BigFix is licensed under the MIT License; see [LICENSE](LICENSE).

- **TCoRFix v1.4** — Nezarn, MIT. Source of the OpenGL extension list,
  texture-unit remap, shader rewrite table, color remap and NV query adapters.
  https://github.com/Nezarn/TCoRFix
- **Ultimate ASI Loader** — ThirteenAG, MIT. Shipped as winmm.dll (v10.0.2).
  https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **MinHook** — Tsuda Kageyu, BSD-2-Clause. Fetched at commit
  `8af6b4acae5a9388fd742b56fa79ece89d96f823`.
  https://github.com/TsudaKageyu/minhook
- **doctest** — Viktor Kirilov and contributors, MIT. Fetched at tag `v2.4.11`.
  https://github.com/doctest/doctest
- **stb** — Sean Barrett and contributors, public domain (also available under MIT).
  Vendored `stb_image_write.h` v1.16 for PNG writing and `stb_image.h` v2.30
  (tests only) for round-trip decoding, from upstream HEAD on 2026-10-29,
  commit `003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5`. Their license texts are
  included in the headers; used under the public-domain option.
  https://github.com/nothings/stb

Third-party projects retain their respective copyrights and licenses.

## Optional DSOAL / OpenAL Soft audio package

These are separately replaceable DLLs under their own LGPL licenses, not MIT
components of BigFix. No audio DLL binaries are committed to this repository.
`tools/package.ps1 -IncludeDsoal` selects the Win64 pair from the upstream
[DSOAL r697 archive](https://github.com/kcat/dsoal/releases/download/archive/DSOAL_r697.zip),
downloaded and independently hashed on 2026-11-01:

| Payload | SHA-256 |
| --- | --- |
| Outer `DSOAL_r697.zip` | `d9ffb95e58cacc7a2830fcf8dbd6a3b2b5f7881d23d5e7d45677680212510ac2` |
| Nested `DSOAL_r697.zip` | `1a7435c2c9d376099b917ba65c8e3180311163e1f918b84de1a4475c314434a0` |
| `dsound.dll` | `3aff3479a359ba2124b51a93d002cf241f849f8e9bacc3d225bfe23e03b31df4` |
| `dsoal-aldrv.dll` | `072a09cdd7dd6fef0cf4ee9709991400c65971ee6ac701642fffe330eebbc3a3` |

- DSOAL — Chris Robinson and contributors, LGPL 2.1;
  [source e11128250439d586483ee6598d1670ab52ab5e37](https://github.com/kcat/dsoal/tree/e11128250439d586483ee6598d1670ab52ab5e37).
- OpenAL Soft — Chris Robinson and contributors, LGPL 2 or later;
  [source 1fbc89b2279280ab89509b5d5adcd43b941ee0a1](https://github.com/kcat/openal-soft/tree/1fbc89b2279280ab89509b5d5adcd43b941ee0a1).

The optional package includes both complete pinned source archives under
`audio/`, their hashes and source links in `audio/SOURCES.md`, and **all**
notices/version records from upstream's `DSOAL/Documentation/`: DSOAL LGPL,
fmt, OpenAL Soft LGPL, Apache-2.0, BSD-3-Clause and PFFFT texts, readmes and
changelog. Embedded data and dependency sources/notices are also retained in
the complete source archives. The upstream version records identify both
commits; OpenAL Soft r10863 was bundled with DSOAL r697. No HRTF configuration
file is installed. See [audio build and verification](docs/audio-backend.md).

The archive URL may be replaced upstream even under the same revision number.
BigFix accepts only the recorded archive, nested archive, source and DLL
hashes; it never silently adopts replacement bytes or `latest-master`.

## TCoRFix v1.4 license

MIT License

Copyright (c) 2024 Nezarn

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


## MinHook license (including Hacker Disassembler Engine notices)

Source: https://raw.githubusercontent.com/TsudaKageyu/minhook/8af6b4acae5a9388fd742b56fa79ece89d96f823/LICENSE.txt

```text
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.
================================================================================
Hacker Disassembler Engine 32 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```


## Ultimate ASI Loader license

Source: https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/v10.0.2/license

```text
MIT License

Copyright (c) 2023 ThirteenAG

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


## doctest license (test dependency)

Source: https://raw.githubusercontent.com/doctest/doctest/v2.4.11/LICENSE.txt

```text
The MIT License (MIT)

Copyright (c) 2016-2023 Viktor Kirilov

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
