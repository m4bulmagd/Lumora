# Qt Quick renderer experiment

`lumora_quick_renderer_benchmark` uses synthetic immutable Gray8 fixtures and the
production C++ Quick image sink. The interface preview remains unchanged; this
executable does not start acquisition or run a live processing pipeline.

Build the QML configuration and target, then run the executable with an explicit
Qt platform/backend. It prints JSON to standard output and Qt diagnostics to
standard error. Each of Original and Compare at 640×480 and 2048×2048 receives
12 warmup submissions and exactly 60 measured submissions. One ticket is admitted
at a time. Qt's actual render callbacks supply the timestamps. Polling for GUI
delivery includes a 1 ms sleep; that delivery distribution describes this harness.

- Preparation: RGB32 allocation and row-by-row copy, excluding protocol validation.
- Admission to consume: accepted prepared submission to entry into its synchronized
  texture/node replacement; preparation is reported separately.
- Consume to frameSwapped: includes texture/node construction and Qt rendering up
  to the direct swap callback. It is not physical scan-out latency.
- GUI delivery: callback time to draining the receipt on the GUI thread.

The p50/p95 values are nearest-rank percentiles of the 60 measured samples. No
screenshots occur inside the timed samples. Set `LUMORA_RENDERER_CAPTURE_DIR` to
save separate synthetic Compare captures at 900×600 and 1280×800 after measurement.

Each case uses a fresh item, so owner maxima are per case. Texture owner counts
measure application-owned QSG texture objects; image counts measure the explicit
RGB32 backing images retained for current/replacement textures. Internal Qt/RHI
staging copies, deferred driver resources, allocator overhead and GPU memory are
not observable through these counters. Old plus replacement nominal texture
bytes are separately reported; a software texture is not GPU storage.

The harness calls `FrameProcessingEngine::plan` with the existing nine processing
and sixteen display buffers and reserves the sum of old/replacement RGB32 images
through `externalSessionStorageBytes`. It verifies the admitted reserve and reports
the assessment's requested storage. This is a real processing preparation
assessment, not a complete live-session assessment: camera/raw pools, fixture
pools, Qt objects, Qt/driver copies and graphics memory are not included in that
external CPU-image reservation. It neither changes algorithms nor expands pools.

Renderer acceptance is for a dedicated unobscured item without layers/effects or
arbitrary ancestor transforms. Positive visible pane intersections, ancestor
visibility/opacity and rectangular clipping are checked. A window swap does not
prove arbitrary scene content was unobscured. Public Qt texture creation also
provides no per-texture asynchronous upload-success callback: known preparation,
node/texture creation, initialization and graph-loss errors are handled, but
otherwise-undetected driver failures are outside this experiment's proof.
