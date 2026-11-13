# SDK overrides

Whole files of the ReXGlue SDK (the pin in `netplay_sdk.txt`) that this build replaces in the SDK tree at configure
time (`CMakeLists.txt`, `DOA4_SDK_OVERRIDES`). Each entry carries the SHA-256 (LF line ends) of the SDK file the override
was made from; configure stops when the SDK's file is a different one, so a new pin cannot silently lose its change.

`post_fx.diff` is the change against the pinned files, for review and for porting to a new pin.

- `src/ui/d3d12/d3d12_presenter.cpp`, `include/rex/ui/d3d12/d3d12_presenter.h`: the picture effects of the Esc menu's
  Shader tab (`src/doa4_picture_fx.h`). `rex_post_fx_set` (exported) stores 16 values; at every paint the D3D12
  presenter draws the guest output through one pixel shader (compiled at first use with D3DCompiler_47.dll) into a
  texture of the frontbuffer's size, which the first paint effect (bilinear, CAS, FSR) then reads instead of the guest
  output.
