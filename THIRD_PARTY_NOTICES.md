# Third-party notices

This port builds the third-party code listed first, and builds on the work credited after it. Where it comes
from, and which files were taken, is in `third_party/README.md`.

## Code in this repository

### GekkoNet (BSD 2-Clause)

Copyright (c) 2024-2026, Jamie Meyer. Upstream: https://github.com/HeatXD/GekkoNet. Used for the lockstep input
exchange of online play (without its ASIO adapter: the port's own transport carries the packets). Licence file:
`third_party/GekkoNet/LICENSE`, reproduced here.

```
BSD 2-Clause License

Copyright (c) 2024-2026, Jamie Meyer

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### zpp_bits (MIT), inside GekkoNet

Copyright (c) 2021 Eyal Z. Upstream: https://github.com/eyalz800/zpp_bits. GekkoNet serialises its packets with it;
the header is taken as GekkoNet ships it (`third_party/GekkoNet/thirdparty/zpp/`). Licence file:
`third_party/GekkoNet/thirdparty/zpp/LICENSE`, reproduced here.

```
MIT License

Copyright (c) 2021 Eyal Z

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

### miniupnpc (BSD 3-Clause)

Copyright (c) 2005-2025, Thomas Bernard. Upstream: https://github.com/miniupnp/miniupnp. Used to ask the router
to open the host's UDP port (UPnP). Licence file: `third_party/miniupnpc/LICENSE`, reproduced here.

```
BSD 3-Clause License

Copyright (c) 2005-2025, Thomas BERNARD
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### DOA2U recomp: Wi-Fi media streaming mode (MIT)

DOA2U recomp by Parker Allan, parkerallan/doa2u-recomp, MIT. Upstream: https://github.com/parkerallan/doa2u-recomp.
`src/net/np_transport.cpp` (`Transport::Impl::WifiOn` / `WifiOff`) is ported from `doa2u/src/nic/nic_net.c`
(`wlan_no_background_scans`, commit `361dccf`): media streaming mode on every connected Wi-Fi interface while the
transport runs, so Windows does not scan for other networks and hold outgoing frames. The DOA2U project states that its
code is MIT ("Project code is MIT", its README, section License) and ships no separate licence file; the MIT
permission notice is reproduced here with the author's name.

```
MIT License

Copyright (c) 2026 Parker Allan

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

### numpy (BSD 3-Clause)

Copyright (c) 2005-2023, NumPy Developers. Upstream: https://github.com/numpy/numpy, tag v1.26.3. The swimsuit
converter (it runs on the player's PC when the DOAX2 swimsuits are imported) must produce the same bits as the Python
script it was made from, so it ports numpy's float64 algorithms line by line, each function citing the numpy file and
line it follows: the default argsort (quicksort with its insertion-sort and heapsort fallbacks) and argmin in
`src/doa4_suitconv_sort.cpp` and `src/doa4_suitconv_sort.h`; the pairwise sum, the add reduction over a contiguous
axis, `mean`, `linspace` and the degrees-to-radians constant in `src/doa4_suitconv_num.cpp`. numpy's licence:

```
Copyright (c) 2005-2023, NumPy Developers.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
       copyright notice, this list of conditions and the following
       disclaimer in the documentation and/or other materials provided
       with the distribution.

    * Neither the name of the NumPy Developers nor the names of any
       contributors may be used to endorse or promote products derived
       from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### CPython (Python Software Foundation License 2.0)

Copyright (c) 2001 Python Software Foundation; All Rights Reserved. Upstream: https://github.com/python/cpython, tag
v3.12.1. The same converter follows CPython's float semantics line by line: `sum` over floats (`Python/bltinmodule.c`),
float floor division, `divmod` and `round`, the 2-byte and 4-byte float pack and unpack (`Objects/floatobject.c`),
`math.dist` (`Modules/mathmodule.c`) and the iteration order of a small `set` of ints (`Objects/setobject.c`). The
ports are in `src/doa4_suitconv_num.cpp`, `src/doa4_suitconv_cloth.cpp`, `src/doa4_suitconv_cloth.h`,
`src/doa4_suitconv_catx.cpp`, `src/doa4_suitconv_catx.h` and `src/doa4_suitconv_be.h`. Summary of the changes made to
Python's code, as the licence asks: the C functions were rewritten in C++ over plain arrays, with CPython's operation
order kept and its exceptions turned into a C++ error type. The licence (PSF License Agreement, Python Software
Foundation License Version 2):

```
PYTHON SOFTWARE FOUNDATION LICENSE VERSION 2

1. This LICENSE AGREEMENT is between the Python Software Foundation
("PSF"), and the Individual or Organization ("Licensee") accessing and
otherwise using this software ("Python") in source or binary form and
its associated documentation.

2. Subject to the terms and conditions of this License Agreement, PSF hereby
grants Licensee a nonexclusive, royalty-free, world-wide license to reproduce,
analyze, test, perform and/or display publicly, prepare derivative works,
distribute, and otherwise use Python alone or in any derivative version,
provided, however, that PSF's License Agreement and PSF's notice of copyright,
i.e., "Copyright (c) 2001 Python Software Foundation; All Rights Reserved"
are retained in Python alone or in any derivative version prepared by Licensee.

3. In the event Licensee prepares a derivative work that is based on
or incorporates Python or any part thereof, and wants to make
the derivative work available to others as provided herein, then
Licensee hereby agrees to include in any such work a brief summary of
the changes made to Python.

4. PSF is making Python available to Licensee on an "AS IS"
basis.  PSF MAKES NO REPRESENTATIONS OR WARRANTIES, EXPRESS OR
IMPLIED.  BY WAY OF EXAMPLE, BUT NOT LIMITATION, PSF MAKES NO AND
DISCLAIMS ANY REPRESENTATION OR WARRANTY OF MERCHANTABILITY OR FITNESS
FOR ANY PARTICULAR PURPOSE OR THAT THE USE OF PYTHON WILL NOT
INFRINGE ANY THIRD PARTY RIGHTS.

5. PSF SHALL NOT BE LIABLE TO LICENSEE OR ANY OTHER USERS OF PYTHON
FOR ANY INCIDENTAL, SPECIAL, OR CONSEQUENTIAL DAMAGES OR LOSS AS
A RESULT OF MODIFYING, DISTRIBUTING, OR OTHERWISE USING PYTHON,
OR ANY DERIVATIVE THEREOF, EVEN IF ADVISED OF THE POSSIBILITY THEREOF.

6. This License Agreement will automatically terminate upon a material
breach of its terms and conditions.

7. Nothing in this License Agreement shall be deemed to create any
relationship of agency, partnership, or joint venture between PSF and
Licensee.  This License Agreement does not grant permission to use PSF
trademarks or trade name in a trademark sense to endorse or promote
products or services of Licensee, or any third party.

8. By copying, installing or otherwise using Python, Licensee
agrees to be bound by the terms and conditions of this License Agreement.
```

### OpenBLAS (BSD 3-Clause): an acknowledgement, no code copied

The converter's matrix-vector and dot-product sums (`src/doa4_suitconv_cloth.cpp`, `Gemv3`, and
`src/doa4_suitconv_num.h`, `NpDotFused`) match the order of arithmetic of the OpenBLAS 0.3.23 kernels numpy 1.26.3 calls
(`dgemv_t_4.c` and the dot kernel), so that the results are the same bits. The arithmetic order was matched; no OpenBLAS
code was copied. OpenBLAS: https://github.com/OpenMathLib/OpenBLAS, Copyright (c) 2011-2014, The OpenBLAS Project, and
others, BSD 3-Clause.

### musl libc (MIT), the exp and atan2 functions

Copyright (c) 2005-2020 Rich Felker, et al. Upstream: https://musl.libc.org, tag v1.2.4. The swimsuit converter may not
call the C runtime's `exp` or `atan2` (their last bit differs between PCs), so `src/doa4_suitconv_num.cpp` carries
ports of musl's `src/math/exp.c` and `exp_data.c` (Copyright (c) 2018, Arm Limited, SPDX-License-Identifier: MIT) and
of `src/math/atan2.c` and `atan.c` (from FreeBSD's `e_atan2.c` and `s_atan.c`, Copyright (C) 1993 by Sun
Microsystems, Inc.). The licence text that file quotes, reproduced here:

```
musl as a whole is licensed under the following standard MIT license:

Copyright (c) 2005-2020 Rich Felker, et al.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Copyright (c) 2018, Arm Limited.
SPDX-License-Identifier: MIT

Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
Developed at SunSoft, a Sun Microsystems, Inc. business.
Permission to use, copy, modify, and distribute this
software is freely granted, provided that this notice
is preserved.
```

## Credits

The same names are in the README's Credits section.

- parkerallan (https://github.com/parkerallan): the original `doa4-recomp` (https://github.com/parkerallan/doa4-recomp)
  this repository is a fork of, and the DOA2U recompilation whose netplay layer (the lobby, join codes, hand-off
  files and connection layer) this port is ported from; GekkoNet and miniupnpc were copied from its `third_party/`
  folder (branch `netplay-release` of the DOA2U recompilation project).
- The ReXGlue SDK (https://github.com/rexglue/rexglue-sdk): the recompilation toolchain and runtime.
- Tekken 6 Native Online, by Ekspym and mahmoud-mess (https://github.com/Ekspym/tekken6-native-online-source,
  https://github.com/mahmoud-mess/tekken6-recomp): the audio and XMA lockstep of the SDK fork this port runs on is
  theirs, and their work is the main reference for the rollback-ready design.
- GekkoNet by HeatXD (https://github.com/HeatXD/GekkoNet), the input exchange, and miniupnpc by Thomas Bernard
  (https://github.com/miniupnp/miniupnp), the router port; their licences are above.
- MK-3D-rollback by Jstackzz (https://github.com/Jstackzz/MK-3D-rollback): studied for the lobby, rotation and
  hold-point design. No code taken.
- Also studied for the rollback research, no code taken: Project Slippi (https://github.com/project-slippi/Ishiiruka),
  Flycast (https://github.com/flyinghead/flycast), Xenia Canary netplay by AdrianCassar
  (https://github.com/AdrianCassar/xenia-canary), RMG-K (https://github.com/Jay-Day/RMG-K), 3SX
  (https://github.com/crowded-street/3sx) and Giuroll (https://github.com/Hagb/giuroll).
- sheikproject (https://github.com/sheikproject): the DOAX2 swimsuit converter is built on their version of the
  original Blender script to import models into DOA4, found on the XeNTaX forums. That script could load a basic
  armature and the models; sheikproject heavily modified and updated it, and the converter's recipe follows their
  version. The original script's author is not known.
- Thanks to everyone who helped playtest the online play.
- GGPO: no GGPO code is taken. GekkoNet is inspired by it and carries no GGPO code, so no GGPO notice is needed.
