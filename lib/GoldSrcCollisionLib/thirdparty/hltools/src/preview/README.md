# Bundled lightmap worker

This target statically links the original CSG/BSP/VIS/RAD and lightmap export
code into a worker shipped by TrenchBroom. Its commands use the same compile
orchestration, option parsing, project extraction and atlas writer as hltools.
The Vulkan compute backend is enabled when an installed SDK and `glslc` are
available at build time. Lightmap preview requests GPU lighting by default;
hltools falls back to its CPU path when no usable Vulkan device is available.
Builds without the SDK retain the same CPU fallback. Embedded source ZIP output
is omitted; SQLite is supplied by the parent build.

Every invocation runs in a fresh process, so compiler globals, fatal exits and
immediate cancellation stay isolated from the editor and from Play collision.
Project assets are extracted under the working directory in this build; the
editor owns and cleans that directory after process exit, including cancellation.
The editor supplies MAP/WAD or a frozen project, thread limit and quality and
reads the existing atlas/manifest output. No separate hltools installation is
needed. Update this source in hltools and sync the pinned snapshot into TB.

Preview compilation accepts `-previewfastvis` for fast VIS with the normal RAD
settings, independently of the shared `-fast` option.

Preview compilation accepts `-noclip -previewcache <prefix>`. The editor owns
this session cache and copies it into each new scratch directory. It contains
VIS output before RAD mutates the BSP, plus its temporary WAD. Only changes to
supported point-light photometric properties and valid interior origins can
reuse geometry; all other MAP text, compiler settings and resolved WAD bytes
must match exactly. Studio BSP, external hulls and custom invisible-item inputs
disable reuse. Missing, oversized or unreadable inputs fall back to a complete
compile. Relighting uses the existing CSG entity replacement, cached visibility
and complete RAD rather than partially updating the cached geometry.
