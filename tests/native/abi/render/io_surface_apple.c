// Sessions that render into a ring of IOSurfaces that the library allocates.
// A case reads a frame's surface on the CPU, as a host can with IOSurfaceLock.
// Builds without Metal reject the ring.

#include "support/frames.h"
#include "support/test_support.h"

#if defined(MLN_FFI_TEST_BACKEND_METAL)
#include <IOSurface/IOSurfaceRef.h>
#include <dlfcn.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <unistd.h>
#endif

enum { ring_width = 64, ring_height = 48, buffer_count = 3 };

static mln_status start_attach(
  mln_map map, mln_render_driver_kind driver,
  mln_gpu_sync_kind_flags producer_sync_kinds, mln_test_completion* completion,
  mln_test_render_fixture* fixture
) {
  const mln_io_surface_ring_descriptor descriptor = {
    .size = sizeof(mln_io_surface_ring_descriptor),
    .logical_size = {.width = ring_width, .height = ring_height},
    .buffer_count = buffer_count,
  };
  mln_render_session_attach_options options = {
    .size = sizeof(mln_render_session_attach_options),
    .driver = driver,
    .producer_sync_kinds = producer_sync_kinds,
  };
  options.frame_wake = mln_test_pulse_wake();
  fixture->driver = driver;
  const mln_status status = mln_map_attach_io_surface_ring(
    map, &descriptor, &options, &fixture->session, &completion->descriptor,
    MLN_TEST_DIAGNOSTIC
  );
  if (status != MLN_STATUS_OK) mln_test_completion_reject(completion);
  return status;
}

// Unity aborts at every assertion after a failed one, back into the case's
// TEST_PROTECT, so a teardown that asserts would run again without end. After
// a failure, the harness reclaims the map and the runtime instead.
static void destroy_map_and_runtime(mln_map map, mln_runtime runtime) {
  if (Unity.CurrentTestFailed) return;
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

#if defined(MLN_FFI_TEST_BACKEND_METAL)

static void attach(
  mln_map map, mln_gpu_sync_kind_flags producer_sync_kinds,
  mln_test_render_fixture* fixture
) {
  mln_test_completion completion = mln_test_completion_default(0);
  MLN_TEST_OK(start_attach(
    map, MLN_RENDER_DRIVER_CORE_WORKER, producer_sync_kinds, &completion,
    fixture
  ));
  MLN_TEST_OK(mln_test_render_fixture_finish_operation(fixture, &completion));
  mln_test_completion_destroy(&completion);
}

static mln_io_surface_frame frame_surface(mln_acquired_frame frame) {
  mln_io_surface_frame surface = {.size = sizeof(mln_io_surface_frame)};
  MLN_TEST_OK(
    mln_acquired_frame_get_io_surface(frame, &surface, MLN_TEST_DIAGNOSTIC)
  );
  TEST_ASSERT_NOT_NULL(surface.surface);
  return surface;
}

// Waits for the frame's rendering through its producer sync, which the
// session keeps.
static void wait_for_producer(mln_acquired_frame frame) {
  mln_gpu_sync sync = {.size = sizeof(mln_gpu_sync)};
  MLN_TEST_OK(
    mln_acquired_frame_get_producer_sync(frame, &sync, MLN_TEST_DIAGNOSTIC)
  );
  if (sync.kind == MLN_GPU_SYNC_CPU_COMPLETE) return;
  TEST_ASSERT_EQUAL_UINT32(MLN_GPU_SYNC_METAL_SHARED_EVENT, sync.kind);
  TEST_ASSERT_TRUE(sync.handle != 0);
  const uint64_t timeout_ms = 10000U * (uint64_t)mln_test_timeout_scale();
  const bool signaled = ((bool (*)(id, SEL, uint64_t, uint64_t))objc_msgSend)(
    (id)(uintptr_t)sync.handle,
    sel_registerName("waitUntilSignaledValue:timeoutMS:"), sync.value,
    timeout_ms
  );
  TEST_ASSERT_TRUE_MESSAGE(signaled, "the frame's shared event never signaled");
}

// Expects the style's red at the center of the frame's surface.
static void expect_red_surface(mln_acquired_frame frame) {
  wait_for_producer(frame);
  IOSurfaceRef surface = frame_surface(frame).surface;
  TEST_ASSERT_EQUAL_INT(
    0, IOSurfaceLock(surface, kIOSurfaceLockReadOnly, NULL)
  );
  const uint8_t* data = IOSurfaceGetBaseAddress(surface);
  const size_t offset =
    (IOSurfaceGetHeight(surface) / 2) * IOSurfaceGetBytesPerRow(surface) +
    (IOSurfaceGetWidth(surface) / 2) * 4;
  uint8_t pixel[4] = {0};
  memcpy(pixel, data + offset, sizeof(pixel));
  TEST_ASSERT_EQUAL_INT(
    0, IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, NULL)
  );
  // BGRA.
  TEST_ASSERT_EQUAL_HEX8(0x00, pixel[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, pixel[1]);
  TEST_ASSERT_EQUAL_HEX8(0xff, pixel[2]);
  TEST_ASSERT_EQUAL_HEX8(0xff, pixel[3]);
}

// Expects the frame's surface to have the size in pixels of `logical_width`
// by `logical_height`.
static void expect_surface_size(
  mln_acquired_frame frame, uint32_t logical_width, uint32_t logical_height
) {
  const mln_io_surface_frame surface = frame_surface(frame);
  uint32_t width = 0;
  uint32_t height = 0;
  MLN_TEST_OK(mln_logical_extent_physical_size(
    (mln_logical_extent){
      .width = logical_width,
      .height = logical_height,
      .scale_factor = surface.scale_factor,
    },
    &width, &height, MLN_TEST_DIAGNOSTIC
  ));
  TEST_ASSERT_EQUAL_UINT32(width, surface.width);
  TEST_ASSERT_EQUAL_UINT32(height, surface.height);
  TEST_ASSERT_EQUAL_size_t(width, IOSurfaceGetWidth(surface.surface));
  TEST_ASSERT_EQUAL_size_t(height, IOSurfaceGetHeight(surface.surface));
  // kCVPixelFormatType_32BGRA.
  TEST_ASSERT_EQUAL_HEX32(0x42475241, IOSurfaceGetPixelFormat(surface.surface));
}

static void release_frame(mln_acquired_frame* frame) {
  if (*frame != MLN_HANDLE_NULL) mln_test_render_release_frame(frame);
}

static void release_session(mln_test_render_fixture* fixture) {
  if (fixture->session != MLN_HANDLE_NULL) {
    (void)mln_test_render_session_release(fixture->session);
  }
  fixture->session = MLN_HANDLE_NULL;
}

static mln_map create_red_map(mln_runtime runtime) {
  const mln_map map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  return map;
}

static void a_frame_renders_into_a_surface_that_the_host_reads(void) {
  const mln_runtime runtime = mln_test_create_runtime();
  const mln_map map = create_red_map(runtime);
  mln_test_render_fixture fixture = {0};
  mln_acquired_frame frame = MLN_HANDLE_NULL;
  if (TEST_PROTECT()) {
    attach(
      map,
      MLN_GPU_SYNC_KIND_FLAG_METAL_SHARED_EVENT |
        MLN_GPU_SYNC_KIND_FLAG_CPU_COMPLETE,
      &fixture
    );
    mln_render_session_capabilities capabilities = {
      .size = sizeof(mln_render_session_capabilities)
    };
    MLN_TEST_OK(mln_render_session_get_capabilities(
      fixture.session, &capabilities, MLN_TEST_DIAGNOSTIC
    ));
    TEST_ASSERT_EQUAL_UINT32(buffer_count, capabilities.texture_ring_depth);
    TEST_ASSERT_TRUE(
      (capabilities.flags & MLN_RENDER_SESSION_CAPABILITY_FRAME_ACQUISITION) !=
      0
    );
    TEST_ASSERT_EQUAL_UINT32(
      MLN_GPU_SYNC_METAL_SHARED_EVENT, capabilities.producer_sync_kind
    );
    TEST_ASSERT_EQUAL_HEX32(
      MLN_GPU_SYNC_KIND_FLAG_CPU_COMPLETE |
        MLN_GPU_SYNC_KIND_FLAG_METAL_SHARED_EVENT,
      capabilities.consumer_sync_kinds
    );

    frame = mln_test_render_and_acquire(&fixture, 1);
    expect_surface_size(frame, ring_width, ring_height);
    expect_red_surface(frame);
    release_frame(&frame);
  }
  release_frame(&frame);
  release_session(&fixture);
  destroy_map_and_runtime(map, runtime);
}

// A host keeps a frame acquired while it shows the frame, so the session
// renders the next frames into the ring's other surfaces.
static void a_held_frame_keeps_its_surface(void) {
  const mln_runtime runtime = mln_test_create_runtime();
  const mln_map map = create_red_map(runtime);
  mln_test_render_fixture fixture = {0};
  mln_acquired_frame held = MLN_HANDLE_NULL;
  mln_acquired_frame next = MLN_HANDLE_NULL;
  if (TEST_PROTECT()) {
    attach(map, 0, &fixture);
    held = mln_test_render_and_acquire(&fixture, 1);
    const mln_io_surface_frame first = frame_surface(held);

    for (uint64_t token = 2; token < 2 + buffer_count; ++token) {
      next = mln_test_render_and_acquire(&fixture, token);
      const mln_io_surface_frame other = frame_surface(next);
      TEST_ASSERT_NOT_EQUAL_UINT32(first.slot, other.slot);
      TEST_ASSERT_TRUE(first.surface != other.surface);
      expect_red_surface(next);
      release_frame(&next);
    }

    const mln_io_surface_frame again = frame_surface(held);
    TEST_ASSERT_EQUAL_UINT32(first.slot, again.slot);
    TEST_ASSERT_TRUE(first.surface == again.surface);
    expect_red_surface(held);
    release_frame(&held);
  }
  release_frame(&next);
  release_frame(&held);
  release_session(&fixture);
  destroy_map_and_runtime(map, runtime);
}

static mln_gpu_sync producer_sync(mln_acquired_frame frame) {
  mln_gpu_sync sync = {.size = sizeof(mln_gpu_sync)};
  MLN_TEST_OK(
    mln_acquired_frame_get_producer_sync(frame, &sync, MLN_TEST_DIAGNOSTIC)
  );
  TEST_ASSERT_EQUAL_UINT32(MLN_GPU_SYNC_METAL_SHARED_EVENT, sync.kind);
  TEST_ASSERT_TRUE(sync.handle != 0);
  return sync;
}

static uint64_t signaled_value(uint64_t event) {
  return ((uint64_t (*)(id, SEL))objc_msgSend)(
    (id)(uintptr_t)event, sel_registerName("signaledValue")
  );
}

static void signal_event(id event, uint64_t value) {
  ((void (*)(id, SEL, uint64_t))objc_msgSend)(
    event, sel_registerName("setSignaledValue:"), value
  );
}

// Releases `*frame` for the session to render into its surface again only
// once `gate` reaches 1 on the GPU.
static void release_frame_behind(mln_acquired_frame* frame, id gate) {
  const mln_gpu_sync sync = {
    .size = sizeof(mln_gpu_sync),
    .kind = MLN_GPU_SYNC_METAL_SHARED_EVENT,
    .handle = (uint64_t)(uintptr_t)gate,
    .value = 1,
  };
  MLN_TEST_OK(mln_acquired_frame_release(*frame, &sync, MLN_TEST_DIAGNOSTIC));
  *frame = MLN_HANDLE_NULL;
}

// A host shows a frame once its shared event reaches the frame's value, while
// the session renders the next. So with frames in flight, each has a value of
// its own, later frames larger ones, and the event reaches a frame's value
// only once that frame's GPU work has ended.
static void frames_in_flight_signal_their_own_values_in_order(void) {
  const mln_runtime runtime = mln_test_create_runtime();
  const mln_map map = create_red_map(runtime);
  mln_test_render_fixture fixture = {0};
  mln_acquired_frame held[buffer_count] = {MLN_HANDLE_NULL};
  mln_acquired_frame first = MLN_HANDLE_NULL;
  mln_acquired_frame second = MLN_HANDLE_NULL;
  // Static: a failed assertion longjmps out of TEST_PROTECT, and the cleanup
  // after it must still see the gate to open it, or the session's queue
  // stays blocked.
  static id gate;
  gate = nil;
  if (TEST_PROTECT()) {
    attach(map, MLN_GPU_SYNC_KIND_FLAG_METAL_SHARED_EVENT, &fixture);
    // Every slot is held, so each next frame goes to the slot released for
    // it.
    for (uint64_t index = 0; index < buffer_count; ++index) {
      held[index] = mln_test_render_and_acquire(&fixture, 1 + index);
    }
    const mln_gpu_sync last = producer_sync(held[buffer_count - 1]);
    wait_for_producer(held[buffer_count - 1]);
    // The session renders with the system's default device, as the
    // descriptor names none.
    const id device =
      ((id (*)(void))dlsym(RTLD_DEFAULT, "MTLCreateSystemDefaultDevice"))();
    TEST_ASSERT_NOT_NULL(device);
    gate = ((id (*)(id, SEL))objc_msgSend)(
      device, sel_registerName("newSharedEvent")
    );
    TEST_ASSERT_NOT_NULL(gate);

    // The first frame waits on the GPU for the gate, and the second runs
    // behind it on the session's queue, so neither ends before the gate.
    release_frame_behind(&held[0], gate);
    first = mln_test_render_and_acquire(&fixture, 1 + buffer_count);
    mln_test_render_release_frame(&held[1]);
    second = mln_test_render_and_acquire(&fixture, 2 + buffer_count);

    const mln_gpu_sync first_sync = producer_sync(first);
    const mln_gpu_sync second_sync = producer_sync(second);
    TEST_ASSERT_EQUAL_UINT64(last.handle, first_sync.handle);
    TEST_ASSERT_EQUAL_UINT64(last.handle, second_sync.handle);
    TEST_ASSERT_GREATER_THAN_UINT64(last.value, first_sync.value);
    TEST_ASSERT_GREATER_THAN_UINT64(first_sync.value, second_sync.value);

    // Long enough for an unblocked frame of this map to end on the GPU.
    usleep(200000);
    TEST_ASSERT_LESS_THAN_UINT64(first_sync.value, signaled_value(last.handle));

    signal_event(gate, 1);
    expect_red_surface(first);
    expect_red_surface(second);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(
      second_sync.value, signaled_value(last.handle)
    );
    release_frame(&first);
    release_frame(&second);
  }
  // A failed case must not leave the session's queue blocked. It asserts
  // nothing more, which would jump back into the case: the harness reclaims
  // the frames, the session, the map and the runtime.
  if (gate != nil) signal_event(gate, 1);
  if (!Unity.CurrentTestFailed) {
    release_frame(&first);
    release_frame(&second);
    for (size_t index = 0; index < buffer_count; ++index) {
      release_frame(&held[index]);
    }
    release_session(&fixture);
  }
  if (gate != nil) {
    ((void (*)(id, SEL))objc_msgSend)(gate, sel_registerName("release"));
  }
  destroy_map_and_runtime(map, runtime);
}

static void a_resize_gives_the_ring_surfaces_of_the_new_size(void) {
  const mln_runtime runtime = mln_test_create_runtime();
  const mln_map map = create_red_map(runtime);
  mln_test_render_fixture fixture = {0};
  mln_acquired_frame frame = MLN_HANDLE_NULL;
  if (TEST_PROTECT()) {
    attach(map, 0, &fixture);
    frame = mln_test_render_and_acquire(&fixture, 1);
    expect_surface_size(frame, ring_width, ring_height);
    release_frame(&frame);

    MLN_TEST_RENDER_AWAIT(
      MLN_STATUS_OK, &fixture,
      mln_render_session_resize(
        fixture.session,
        (mln_logical_size){.width = ring_height, .height = ring_width},
        &completion.descriptor, MLN_TEST_DIAGNOSTIC
      )
    );
    frame = mln_test_render_and_acquire(&fixture, 2);
    expect_surface_size(frame, ring_height, ring_width);
    expect_red_surface(frame);
    release_frame(&frame);
  }
  release_frame(&frame);
  release_session(&fixture);
  destroy_map_and_runtime(map, runtime);
}

// The library owns the command queue, so it drives the session itself.
static void an_io_surface_ring_rejects_the_callers_graphics_thread(void) {
  const mln_runtime runtime = mln_test_create_runtime();
  const mln_map map = mln_test_create_map(runtime);
  if (TEST_PROTECT()) {
    mln_test_render_fixture fixture = {0};
    mln_test_completion completion = mln_test_completion_default(0);
    MLN_TEST_STATUS(
      MLN_STATUS_UNSUPPORTED,
      start_attach(
        map, MLN_RENDER_DRIVER_CALLER_GRAPHICS_THREAD, 0, &completion, &fixture
      )
    );
    mln_test_completion_destroy(&completion);
    TEST_ASSERT_EQUAL_UINT64(MLN_HANDLE_NULL, fixture.session);
  }
  destroy_map_and_runtime(map, runtime);
}

#else

static void an_io_surface_ring_needs_the_metal_backend(void) {
  const mln_runtime runtime = mln_test_create_runtime();
  const mln_map map = mln_test_create_map(runtime);
  if (TEST_PROTECT()) {
    mln_test_render_fixture fixture = {0};
    mln_test_completion completion = mln_test_completion_default(0);
    MLN_TEST_STATUS(
      MLN_STATUS_UNSUPPORTED,
      start_attach(map, MLN_RENDER_DRIVER_CORE_WORKER, 0, &completion, &fixture)
    );
    mln_test_completion_destroy(&completion);
    TEST_ASSERT_EQUAL_UINT64(MLN_HANDLE_NULL, fixture.session);
  }
  destroy_map_and_runtime(map, runtime);
}

#endif

static void getting_an_io_surface_needs_a_live_frame(void) {
  mln_io_surface_frame surface = {.size = sizeof(mln_io_surface_frame)};
  MLN_TEST_INVALID(
    mln_acquired_frame_get_io_surface(MLN_HANDLE_NULL, &surface, NULL)
  );
}

MLN_TEST_GROUP {
#if defined(MLN_FFI_TEST_BACKEND_METAL)
  RUN_TEST(a_frame_renders_into_a_surface_that_the_host_reads);
  RUN_TEST(a_held_frame_keeps_its_surface);
  RUN_TEST(frames_in_flight_signal_their_own_values_in_order);
  RUN_TEST(a_resize_gives_the_ring_surfaces_of_the_new_size);
  RUN_TEST(an_io_surface_ring_rejects_the_callers_graphics_thread);
#else
  RUN_TEST(an_io_surface_ring_needs_the_metal_backend);
#endif
  RUN_TEST(getting_an_io_surface_needs_a_live_frame);
}
