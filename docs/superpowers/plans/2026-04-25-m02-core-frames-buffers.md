# Milestone 2 Core Frames and Bounded Buffers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement immutable frame/value types, checked image validation, reusable bounded memory, latest-value exchange, queues, and testable clocks.

**Architecture:** `lumora_core` owns dependency-free C++20 primitives. Writable pool leases are sealed before publication, and every exchange exposes replacement or exhaustion explicitly.

**Tech Stack:** C++20 standard library, GCC/MSVC, CMake, GoogleTest/CTest.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

## Global Constraints

- This milestone contributes only to the open-source evaluation release, which must display `EVALUATION — NOT FOR CLINICAL USE` and must not acquire or store real patient data.
- Unless a step is explicitly Windows packaging or hardware work, execute it on Linux/GCC and require the matching Windows/MSVC simulator CI job before milestone acceptance.
- Preserve the fixed versioned processing order, native-orientation Original storage, shared installation orientation, bounded freshness, and mandatory paused/stale indications wherever this milestone touches them.
- Lumora-owned code uses Apache-2.0; dependencies stay pinned and target-scoped, pylon remains optional/external, and only dynamically linked LGPL-compatible Qt modules may enter distributed builds.

- `lumora_core` has no Qt, OpenCV, spdlog, or pylon dependency.
- Frames are immutable after publication and use checked size/stride arithmetic.
- Live exchanges are capacity one; FIFO queues have constructor-fixed capacity.
- No operation allocates an emergency fallback buffer after pool initialization.
- Use `std::chrono::steady_clock` for durations and a separate UTC timestamp for metadata.

---

### Task 1: Harden typed results and add checked arithmetic

**Files:**
- Modify: `src/core/include/lumora/core/Error.hpp`
- Modify: `src/core/include/lumora/core/Result.hpp`
- Create: `src/core/include/lumora/core/CheckedMath.hpp`
- Create: `src/core/src/Error.cpp`
- Create: `tests/unit/core/ResultTests.cpp`
- Create: `tests/unit/core/CheckedMathTests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the Milestone 1 `Error` and `Result<T, Error>` contracts.
- Produces: `ErrorCategory`, `Error`, `Result<T, Error>`, `Result<void, Error>`, `checkedMultiply(size_t,size_t)`, and `checkedAdd(size_t,size_t)`.

- [ ] **Step 1: Write failing result and overflow tests**

```cpp
TEST(CheckedMath, RejectsSizeOverflow) {
    EXPECT_FALSE(checkedMultiply(std::numeric_limits<size_t>::max(), 2).hasValue());
}

TEST(Result, PreservesTypedError) {
    Error error{ErrorCategory::InvalidFrame, "payload_small", "Invalid frame", "payload is short", false};
    Result<int> result = Result<int>::failure(error);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "payload_small");
}
```

- [ ] **Step 2: Build to verify checked arithmetic is still missing**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_core_tests`

Expected: FAIL on the missing `checkedMultiply` function while the existing result smoke test still compiles.

- [ ] **Step 3: Complete the value-contract tests and implement checked arithmetic**

```cpp
enum class ErrorCategory {
    CameraDiscovery, CameraConnection, CameraConfiguration, Acquisition,
    InvalidFrame, Processing, Configuration, Encoding, Storage,
    ResourceExhaustion, Cancelled, Internal
};

struct Error {
    ErrorCategory category;
    std::string code;
    std::string operatorSummary;
    std::string diagnosticDetail;
    bool recoverable;
    std::optional<std::int64_t> nativeCode;
};
```

`Result` must never expose `value()` when holding an error and must support move-only values.

- [ ] **Step 4: Run focused tests**

Run: `ctest --preset linux-gcc-debug-sim --output-on-failure -R "Result|CheckedMath"`

Expected: PASS including zero, maximum, and overflow cases.

- [ ] **Step 5: Commit core result primitives**

```powershell
git add src/core tests/unit/core src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(core): add typed results and checked arithmetic"
```

### Task 2: Pixel formats, layout validation, and frame metadata

**Files:**
- Create: `src/core/include/lumora/core/PixelFormat.hpp`
- Create: `src/core/include/lumora/core/ImageLayout.hpp`
- Create: `src/core/include/lumora/core/FrameMetadata.hpp`
- Create: `src/core/src/ImageLayout.cpp`
- Create: `tests/unit/core/ImageLayoutTests.cpp`
- Create: `tests/unit/core/FrameMetadataTests.cpp`

**Interfaces:**
- Consumes: `Result`, `Error`, and checked arithmetic.
- Produces: `StorageType`, capability-derived `SourcePixelFormat`, `DisplayStorage`, `ImageLayout::create`, `CameraIdentity`, `RegionOfInterest`, and `AcquisitionSettingsSnapshot`.

- [ ] **Step 1: Write validation tests for valid and malformed layouts**

```cpp
TEST(ImageLayout, AcceptsPaddedMono16Rows) {
    auto layout = ImageLayout::create(2048, 2048, 4160, StorageType::UInt16, 4160ULL * 2048ULL);
    ASSERT_TRUE(layout.hasValue());
    EXPECT_EQ(layout.value().rowBytes(), 4096U);
}

TEST(ImageLayout, RejectsShortPayload) {
    auto layout = ImageLayout::create(32, 32, 64, StorageType::UInt16, 2047);
    ASSERT_FALSE(layout.hasValue());
    EXPECT_EQ(layout.error().code, "payload_too_small");
}
```

- [ ] **Step 2: Verify tests fail before implementation**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_core_tests`

Expected: FAIL on missing layout types.

- [ ] **Step 3: Implement exact data contracts**

```cpp
enum class StorageType { UInt8, UInt16 };
enum class DisplayStorage { Gray8, Gray16 };

struct SourcePixelFormat {
    std::string canonicalName;
    std::uint32_t canonicalEncoding;
    std::uint8_t validBits;
    std::uint16_t sampleMaximum;
    SourcePacking packing;
    BitAlignment alignment;
    StorageType applicationStorage;
};

struct ImageLayout {
    static Result<ImageLayout> create(std::uint32_t width, std::uint32_t height,
        std::size_t strideBytes, StorageType storage, std::size_t payloadBytes);
    std::size_t rowBytes() const noexcept;
};
```

`AcquisitionSettingsSnapshot` must carry the full source format descriptor, ROI, requested/actual FPS, optional exposure microseconds, and optional gain dB. Validate `sampleMaximum` against storage and valid bits. `CameraIdentity` carries manufacturer, model, serial, transport, and optional firmware. Do not freeze a four-value camera-format enum before the Milestone 6 hardware gate.

- [ ] **Step 4: Add boundary cases**

Cover zero dimensions, stride smaller than row bytes, overflow in `stride * height`, unsupported valid-bit count, and payload exactly equal to the required bytes.

- [ ] **Step 5: Run core tests and commit**

Run: `ctest --preset linux-gcc-debug-sim --output-on-failure -R "ImageLayout|FrameMetadata"`

```powershell
git add src/core tests/unit/core
git commit -m "feat(core): add validated image layouts and metadata"
```

### Task 3: Fixed-capacity buffer pool and immutable sealing

**Files:**
- Create: `src/core/include/lumora/core/BufferPool.hpp`
- Create: `src/core/include/lumora/core/SharedBuffer.hpp`
- Create: `src/core/src/BufferPool.cpp`
- Create: `tests/unit/core/BufferPoolTests.cpp`

**Interfaces:**
- Consumes: checked arithmetic and typed resource-exhaustion errors.
- Produces: `BufferPool::create`, `BufferPool::tryAcquire`, `WritableBufferLease::bytes`, `WritableBufferLease::seal`, `SharedBuffer::bytes`, and `BufferPoolStats`.

- [ ] **Step 1: Write failing capacity and lifetime tests**

```cpp
TEST(BufferPool, NeverGrowsPastConfiguredCapacity) {
    auto pool = BufferPool::create(2, 4096).value();
    auto first = pool->tryAcquire();
    auto second = pool->tryAcquire();
    EXPECT_TRUE(first.has_value());
    EXPECT_TRUE(second.has_value());
    EXPECT_FALSE(pool->tryAcquire().has_value());
    first.reset();
    EXPECT_TRUE(pool->tryAcquire().has_value());
}
```

- [ ] **Step 2: Verify the test fails for missing pool types**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_core_tests`

Expected: FAIL.

- [ ] **Step 3: Implement preallocation and sealing**

```cpp
class WritableBufferLease final {
public:
    std::span<std::byte> bytes() noexcept;
    SharedBuffer seal() &&;
};

class BufferPool final {
public:
    static Result<std::shared_ptr<BufferPool>> create(std::size_t capacity,
                                                       std::size_t bytesPerBuffer);
    std::optional<WritableBufferLease> tryAcquire() noexcept;
    BufferPoolStats stats() const noexcept;
};
```

Returned `SharedBuffer` exposes only `std::span<const std::byte>`. The final shared reference returns the block to the pool. Pool destruction waits until the owner stops workers; a lease safely retains the pool state until release.

- [ ] **Step 4: Add concurrency and destruction tests**

Use multiple producer threads to acquire/release, assert the in-use count never exceeds capacity, high-water mark is exact, and release after the external pool handle is dropped is safe.

- [ ] **Step 5: Run with MSVC AddressSanitizer configuration**

Run the focused test in the repository's sanitizer preset once added; until then run Debug with page heap disabled and ensure no invalid access under 100,000 acquire/release operations.

- [ ] **Step 6: Commit buffer ownership**

```powershell
git add src/core tests/unit/core/BufferPoolTests.cpp
git commit -m "feat(core): add fixed reusable buffer pool"
```

### Task 4: Immutable frame family and pairing

**Files:**
- Create: `src/core/include/lumora/core/Frame.hpp`
- Create: `src/core/src/Frame.cpp`
- Create: `tests/unit/core/FrameTests.cpp`

**Interfaces:**
- Consumes: `ImageLayout`, `SharedBuffer`, `CameraIdentity`, and `AcquisitionSettingsSnapshot`.
- Produces: `RawFrame::create`, `ProcessedFrame::create`, format-aware `DisplayFrame::create`, and `FrameBundle::create`.

- [ ] **Step 1: Write failing immutability and pairing tests**

```cpp
TEST(FrameBundle, RejectsMismatchedFrameIds) {
    auto raw = makeRawFrame(41);
    auto display = makeDisplayFrame(42);
    auto bundle = FrameBundle::create(raw, display, {}, {});
    ASSERT_FALSE(bundle.hasValue());
    EXPECT_EQ(bundle.error().code, "frame_id_mismatch");
}
```

- [ ] **Step 2: Verify missing frame contracts fail**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_core_tests`

Expected: FAIL.

- [ ] **Step 3: Implement frame factories with validation**

```cpp
struct RawFrame final {
    std::uint64_t frameId;
    ImageLayout layout;
    SharedBuffer pixels;
    FrameMetadata metadata;
    static Result<std::shared_ptr<const RawFrame>> create(/* validated fields */);
};
```

All factories accept sealed buffers, compare layout payload requirements with buffer size, and return `std::shared_ptr<const ...>`. `DisplayFrame` records `Gray8` or `Gray16`, display mapping, and presentation orientation even though evaluation composition initially produces `Gray8`. `FrameBundle::create` rejects mismatched IDs and dimensions that cannot represent the declared presentation orientation.

- [ ] **Step 4: Test original bytes remain unchanged**

Hash the raw byte span, construct processed/display frames and a bundle, mutate only independent writable test buffers, and assert the raw hash is unchanged.

- [ ] **Step 5: Run and commit**

Run: `ctest --preset linux-gcc-debug-sim --output-on-failure -R Frame`

```powershell
git add src/core tests/unit/core/FrameTests.cpp
git commit -m "feat(core): add immutable paired frame types"
```

### Task 5: Latest-value slot, bounded FIFO, and clocks

**Files:**
- Create: `src/core/include/lumora/core/LatestValueSlot.hpp`
- Create: `src/core/include/lumora/core/BoundedQueue.hpp`
- Create: `src/core/include/lumora/core/Clock.hpp`
- Create: `src/core/src/Clock.cpp`
- Create: `tests/unit/core/LatestValueSlotTests.cpp`
- Create: `tests/unit/core/BoundedQueueTests.cpp`
- Create: `tests/unit/core/ClockTests.cpp`

**Interfaces:**
- Consumes: immutable shared frame values and C++20 stop tokens.
- Produces: `LatestValueSlot<T>::publish`, `consumeAfter`, `waitForNewer`, `close`; `BoundedQueue<T>::tryPush`, `waitPop`, `close`; `IClock`, `SystemClock`, and test `ManualClock`.

- [ ] **Step 1: Write failing replacement and boundedness tests**

```cpp
TEST(LatestValueSlot, ConsumerReceivesNewestAndCountsReplacement) {
    LatestValueSlot<int> slot;
    EXPECT_FALSE(slot.publish(std::make_shared<const int>(1)).replacedUnconsumed);
    EXPECT_TRUE(slot.publish(std::make_shared<const int>(2)).replacedUnconsumed);
    auto item = slot.consumeAfter(0).value();
    EXPECT_EQ(*item.value, 2);
}

TEST(BoundedQueue, RejectsPushAtCapacity) {
    BoundedQueue<int> queue(2);
    EXPECT_TRUE(queue.tryPush(1));
    EXPECT_TRUE(queue.tryPush(2));
    EXPECT_FALSE(queue.tryPush(3));
}
```

- [ ] **Step 2: Verify tests fail before templates exist**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_core_tests`

Expected: FAIL.

- [ ] **Step 3: Implement exact exchange contracts**

```cpp
template<class T> struct PublishedValue {
    std::uint64_t revision;
    std::shared_ptr<const T> value;
};

template<class T> struct PublishResult {
    std::uint64_t revision;
    bool replacedUnconsumed;
};
```

Both containers use constructor-fixed storage, wake blocked consumers on close or stop request, and never return a reference into internal mutable storage.

- [ ] **Step 4: Add concurrency and cancellation tests**

Run 100,000 publications with a deliberately slow consumer and assert values are monotonic, final value is observed, capacity never exceeds one, replacement occurs, and stop/close wakes within 250 ms. For FIFO, assert ordering, fixed capacity, and cancellation.

- [ ] **Step 5: Implement clocks**

`IClock` returns steady time and UTC time separately. `ManualClock` advances only when a test requests it, allowing deterministic simulator pacing and reconnect delays.

- [ ] **Step 6: Run full core suite and commit**

Run: `ctest --preset linux-gcc-debug-sim --output-on-failure -R Core`

```powershell
git add src/core tests/unit/core
git commit -m "feat(core): add bounded exchanges and clocks"
```

## Milestone 2 acceptance gate

- [ ] `lumora_core` links no Qt, OpenCV, spdlog, or pylon target.
- [ ] Frames expose no mutable pixels after publication.
- [ ] Pool capacity/exhaustion and latest-slot replacement are deterministic under concurrency.
- [ ] Layout overflow and malformed payload tests pass.
- [ ] Stop requests wake all blocking primitives within the tested bound.
