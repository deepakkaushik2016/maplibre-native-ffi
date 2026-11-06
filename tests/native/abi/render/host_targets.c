// Targets that the host creates: a session renders into a borrowed ring and
// presents to a surface, both made by tests/graphics for the preset's backend.
// The browser presets have no such fixtures, because JavaScript owns their
// canvases.

// OpenHarmony's fortified poll.h uses size_t without declaring it.
#include <stddef.h>

#if defined(MLN_FFI_TEST_OPENGL_EGL)
#include <EGL/egl.h>
#endif
#if defined(MLN_FFI_TEST_BACKEND_VULKAN)
#include <vulkan/vulkan_core.h>
#endif
#if !defined(_WIN32)
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

#include "support/frames.h"
#include "support/host_graphics.h"
#include "support/test_support.h"

#if !defined(__EMSCRIPTEN__)
static uint32_t render_frame(
  const mln_test_render_fixture* fixture, uint64_t token
) {
  mln_frame_demand demand = {.size = sizeof(mln_frame_demand)};
  demand.flags = MLN_FRAME_DEMAND_FORCE;
  demand.token = token;
  MLN_TEST_OK(mln_render_session_demand_frame(fixture->session, &demand, NULL));
  const mln_render_frame_batch batch =
    mln_test_render_wait_for_results(fixture);
  const mln_render_frame_result result = mln_test_render_batch_result(batch, 0);
  mln_render_frame_batch_release(batch);
  TEST_ASSERT_EQUAL_UINT64(token, result.token);
  return result.disposition;
}

// Reads back the fixture's texture, and returns the status of the submission
// when it is refused or of the completion when it is accepted.
static mln_status read_back(const mln_test_render_fixture* fixture) {
  mln_test_completion readback = mln_test_completion_readback();
  const mln_status submitted = mln_render_session_read_texture(
    fixture->session, &readback.descriptor, NULL
  );
  if (submitted != MLN_STATUS_OK) {
    mln_test_completion_reject(&readback);
    mln_test_completion_destroy(&readback);
    return submitted;
  }
  const mln_status status =
    mln_test_render_fixture_finish_operation(fixture, &readback);
  mln_test_completion_destroy(&readback);
  return status;
}

static mln_render_session_snapshot read_snapshot(
  const mln_test_render_fixture* fixture
) {
  mln_render_session_snapshot snapshot = {
    .size = sizeof(mln_render_session_snapshot)
  };
  MLN_TEST_OK(
    mln_render_session_get_snapshot(fixture->session, &snapshot, NULL)
  );
  return snapshot;
}

enum { pixel_count = MLN_TEST_HOST_TARGET_SIZE * MLN_TEST_HOST_TARGET_SIZE };

static const uint8_t red[4] = {255, 0, 0, 255};
static const uint8_t blue[4] = {0, 0, 255, 255};

// Every pixel of `texture` holds `color`. Reading another graphics object's
// texture leaves the fixture's OpenGL context as it was.
static void expect_texture_color(
  mln_test_graphics_texture* texture, const uint8_t color[4], const char* what
) {
  static uint8_t pixels[pixel_count * 4];
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_graphics_texture_read_rgba8(texture, pixels, sizeof(pixels)),
    mln_test_graphics_last_error()
  );
  size_t matching = 0;
  for (size_t index = 0; index < pixel_count; index += 1) {
    matching += memcmp(&pixels[index * 4], color, 4) == 0;
  }
  TEST_ASSERT_EQUAL_size_t_MESSAGE(pixel_count, matching, what);
}

// Repaints the red style's background blue, with no transition, so the next
// frame shows the change.
static void paint_background_blue(mln_runtime runtime, mln_map map) {
  MLN_TEST_AWAIT_OK(mln_map_set_style_layer_property(
    map, MLN_BUFFER_LITERAL("bg"),
    MLN_BUFFER_LITERAL("background-color-transition"),
    MLN_BUFFER_LITERAL("{\"duration\":0}"), &completion.descriptor, NULL
  ));
  MLN_TEST_AWAIT_OK(mln_map_set_style_layer_property(
    map, MLN_BUFFER_LITERAL("bg"), MLN_BUFFER_LITERAL("background-color"),
    MLN_BUFFER_LITERAL("\"#0000ff\""), &completion.descriptor, NULL
  ));
  MLN_TEST_OK(mln_test_map_fence(map));
}

// Acquires the frame that a demand just rendered, and checks that it names the
// ring texture of its slot and needs no producer synchronization object, so
// the case reads that texture with no fence of its own. Returns that slot.
static uint32_t acquire_ring_frame(
  const mln_test_render_fixture* fixture, mln_acquired_frame* out_frame
) {
  MLN_TEST_OK(
    mln_render_session_acquire_frame(fixture->session, out_frame, NULL)
  );
  mln_gpu_sync producer = {.size = sizeof(mln_gpu_sync)};
  MLN_TEST_OK(
    mln_acquired_frame_get_producer_sync(*out_frame, &producer, NULL)
  );
  TEST_ASSERT_EQUAL_UINT32(MLN_GPU_SYNC_CPU_COMPLETE, producer.kind);
  uint32_t slot = UINT32_MAX;
  const uint64_t texture = mln_test_frame_texture_handle(*out_frame, &slot);
  TEST_ASSERT_LESS_THAN_UINT32(2, slot);
  TEST_ASSERT_EQUAL_UINT64(
    mln_test_texture_handle(mln_test_render_fixture_texture(fixture, slot)),
    texture
  );
  return slot;
}

static void a_borrowed_ring_hands_each_frame_to_the_host(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  mln_test_render_fixture fixture = {0};
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_render_fixture_create_borrowed_ring(
      map, &fixture, 2, MLN_TEST_PRESET_DRIVER
    ),
    mln_test_graphics_last_error()
  );
  mln_render_session_capabilities capabilities = {
    .size = sizeof(mln_render_session_capabilities)
  };
  MLN_TEST_OK(
    mln_render_session_get_capabilities(fixture.session, &capabilities, NULL)
  );
  TEST_ASSERT_EQUAL_UINT32(2, capabilities.texture_ring_depth);
  TEST_ASSERT_EQUAL_HEX32(
    MLN_RENDER_SESSION_CAPABILITY_FRAME_ACQUISITION, capabilities.flags
  );
  TEST_ASSERT_EQUAL_UINT32(
    MLN_GPU_SYNC_CPU_COMPLETE, capabilities.producer_sync_kind
  );
  TEST_ASSERT_BITS_HIGH(
    MLN_GPU_SYNC_KIND_FLAG_CPU_COMPLETE, capabilities.consumer_sync_kinds
  );

  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 1)
  );
  mln_acquired_frame first = MLN_HANDLE_NULL;
  const uint32_t first_slot = acquire_ring_frame(&fixture, &first);
  expect_texture_color(
    mln_test_render_fixture_texture(&fixture, first_slot), red,
    "the first frame's texture"
  );

  paint_background_blue(runtime, map);
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 2)
  );
  mln_acquired_frame second = MLN_HANDLE_NULL;
  const uint32_t second_slot = acquire_ring_frame(&fixture, &second);
  TEST_ASSERT_NOT_EQUAL_UINT32(first_slot, second_slot);
  expect_texture_color(
    mln_test_render_fixture_texture(&fixture, second_slot), blue,
    "the second frame's texture"
  );
  expect_texture_color(
    mln_test_render_fixture_texture(&fixture, first_slot), red,
    "the held first frame's texture"
  );

  // Both textures are held, so the next demand parks. The maintenance command
  // runs after it on the driver, so once that completes the demand has had its
  // chance to render.
  mln_test_render_demand_forced(&fixture, 3);
  MLN_TEST_RENDER_AWAIT(
    MLN_STATUS_OK, &fixture,
    mln_render_session_reduce_memory_use(
      fixture.session, &completion.descriptor, NULL
    )
  );
  mln_render_frame_batch batch = MLN_HANDLE_NULL;
  MLN_TEST_OK(mln_test_drain_frame_results(fixture.session, &batch, NULL));
  TEST_ASSERT_EQUAL_UINT64(MLN_HANDLE_NULL, batch);
  mln_test_render_release_frame(&first);
  batch = mln_test_render_wait_for_results(&fixture);
  const mln_render_frame_result result = mln_test_render_batch_result(batch, 0);
  mln_render_frame_batch_release(batch);
  TEST_ASSERT_EQUAL_UINT64(3, result.token);
  TEST_ASSERT_EQUAL_UINT32(MLN_RENDER_RESULT_RENDERED, result.disposition);
  mln_acquired_frame third = MLN_HANDLE_NULL;
  TEST_ASSERT_EQUAL_UINT32(first_slot, acquire_ring_frame(&fixture, &third));
  expect_texture_color(
    mln_test_render_fixture_texture(&fixture, first_slot), blue,
    "the released texture after the parked demand"
  );
  mln_test_render_release_frame(&second);
  mln_test_render_release_frame(&third);

  MLN_TEST_STATUS(MLN_STATUS_UNSUPPORTED, read_back(&fixture));
  const mln_logical_size smaller = {
    .width = MLN_TEST_HOST_TARGET_SIZE / 2,
    .height = MLN_TEST_HOST_TARGET_SIZE / 2,
  };
  mln_test_completion resize = mln_test_completion_default(0);
  MLN_TEST_STATUS(
    MLN_STATUS_UNSUPPORTED,
    mln_render_session_resize(
      fixture.session, smaller, &resize.descriptor, MLN_TEST_DIAGNOSTIC
    )
  );
  TEST_ASSERT_NOT_NULL(strstr(mln_test_last_error(), "sized by its owner"));
  mln_test_completion_reject(&resize);
  mln_test_completion_destroy(&resize);

  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

#if defined(MLN_FFI_TEST_BACKEND_VULKAN) || defined(MLN_FFI_TEST_BACKEND_METAL)
static void queue_ordered_frames_share_the_hosts_queue(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  mln_test_render_fixture fixture = {0};
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_render_fixture_create_synced(
      map, &fixture,
      MLN_GPU_SYNC_KIND_FLAG_CPU_COMPLETE | MLN_GPU_SYNC_KIND_FLAG_QUEUE_ORDER
    ),
    mln_test_graphics_last_error()
  );
  mln_render_session_capabilities capabilities = {
    .size = sizeof(mln_render_session_capabilities)
  };
  MLN_TEST_OK(
    mln_render_session_get_capabilities(fixture.session, &capabilities, NULL)
  );
  TEST_ASSERT_EQUAL_UINT32(
    MLN_GPU_SYNC_QUEUE_ORDER, capabilities.producer_sync_kind
  );
  TEST_ASSERT_BITS_HIGH(
    MLN_GPU_SYNC_KIND_FLAG_QUEUE_ORDER, capabilities.consumer_sync_kinds
  );

  const uint8_t* const colors[] = {red, blue};
  for (uint64_t index = 0; index < 2; index += 1) {
    if (index == 1) paint_background_blue(runtime, map);
    TEST_ASSERT_EQUAL_UINT32(
      MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, index + 1)
    );
    mln_acquired_frame frame = MLN_HANDLE_NULL;
    MLN_TEST_OK(
      mln_render_session_acquire_frame(fixture.session, &frame, NULL)
    );
    mln_gpu_sync producer = {.size = sizeof(mln_gpu_sync)};
    MLN_TEST_OK(mln_acquired_frame_get_producer_sync(frame, &producer, NULL));
    TEST_ASSERT_EQUAL_UINT32(MLN_GPU_SYNC_QUEUE_ORDER, producer.kind);
    TEST_ASSERT_EQUAL_UINT64(0, producer.handle);
    uint32_t slot = UINT32_MAX;
    TEST_ASSERT_NOT_EQUAL_UINT64(
      0, mln_test_frame_texture_handle(frame, &slot)
    );
    TEST_ASSERT_EQUAL_UINT32(0, slot);
    expect_texture_color(
      mln_test_render_fixture_texture(&fixture, slot), colors[index],
      "the queue-ordered frame's texture"
    );
    const mln_gpu_sync consumer = {
      .size = sizeof(mln_gpu_sync), .kind = MLN_GPU_SYNC_QUEUE_ORDER
    };
    MLN_TEST_OK(mln_acquired_frame_release(frame, &consumer, NULL));
  }
  // Detach right after a queue-ordered frame and its release, which may both
  // still run on the queue, waits for them before it destroys the ring.
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 3)
  );
  mln_acquired_frame last = MLN_HANDLE_NULL;
  MLN_TEST_OK(mln_render_session_acquire_frame(fixture.session, &last, NULL));
  const mln_gpu_sync queued = {
    .size = sizeof(mln_gpu_sync), .kind = MLN_GPU_SYNC_QUEUE_ORDER
  };
  MLN_TEST_OK(mln_acquired_frame_release(last, &queued, NULL));
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

// A queue-ordered release leaves the host's read of the frame on the shared
// queue, so a replacement of the ring waits for it before the session lets go
// of the old texture.
static void a_queue_ordered_ring_takes_new_textures_after_the_hosts_reads(
  void
) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  mln_test_render_fixture fixture = {0};
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_render_fixture_create_synced(
      map, &fixture, MLN_GPU_SYNC_KIND_FLAG_QUEUE_ORDER
    ),
    mln_test_graphics_last_error()
  );
  const mln_gpu_sync consumer = {
    .size = sizeof(mln_gpu_sync), .kind = MLN_GPU_SYNC_QUEUE_ORDER
  };
  mln_test_graphics_texture* smaller = NULL;
  for (uint64_t index = 0; index < 2; index += 1) {
    if (index == 1) {
      smaller = mln_test_render_fixture_new_sized_texture(
        &fixture, MLN_TEST_HOST_TARGET_SIZE / 2, MLN_TEST_HOST_TARGET_SIZE / 2
      );
      TEST_ASSERT_NOT_NULL_MESSAGE(smaller, mln_test_graphics_last_error());
      MLN_TEST_RENDER_AWAIT(
        MLN_STATUS_OK, &fixture,
        mln_test_render_fixture_set_textures(
          &fixture, &smaller, 1, &completion.descriptor
        )
      );
      // A later query completes after the resize that the replacement made.
      MLN_TEST_OK(mln_test_map_fence(map));
    }
    TEST_ASSERT_EQUAL_UINT32(
      MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, index + 1)
    );
    mln_acquired_frame frame = MLN_HANDLE_NULL;
    MLN_TEST_OK(
      mln_render_session_acquire_frame(fixture.session, &frame, NULL)
    );
    if (smaller != NULL) {
      uint32_t slot = UINT32_MAX;
      TEST_ASSERT_EQUAL_UINT64(
        mln_test_texture_handle(smaller),
        mln_test_frame_texture_handle(frame, &slot)
      );
    }
    MLN_TEST_OK(mln_acquired_frame_release(frame, &consumer, NULL));
  }
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}
#endif

#if defined(MLN_FFI_TEST_BACKEND_VULKAN)
// The Android emulator's Vulkan device has no timeline semaphores; every other
// CI driver does.
#if defined(__ANDROID__)
#define TIMELINES_REQUIRED 0
#else
#define TIMELINES_REQUIRED 1
#endif

static void vulkan_timelines_order_frame_reads_and_slot_reuse(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  mln_test_render_fixture fixture = {0};
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_render_fixture_create_synced(
      map, &fixture,
      MLN_GPU_SYNC_KIND_FLAG_CPU_COMPLETE |
        MLN_GPU_SYNC_KIND_FLAG_VULKAN_TIMELINE_SEMAPHORE
    ),
    mln_test_graphics_last_error()
  );
  mln_render_session_capabilities capabilities = {
    .size = sizeof(mln_render_session_capabilities)
  };
  MLN_TEST_OK(
    mln_render_session_get_capabilities(fixture.session, &capabilities, NULL)
  );
  if (
    capabilities.producer_sync_kind != MLN_GPU_SYNC_VULKAN_TIMELINE_SEMAPHORE
  ) {
    TEST_ASSERT_FALSE_MESSAGE(
      TIMELINES_REQUIRED, "this target's driver has no timeline semaphores"
    );
    TEST_ASSERT_EQUAL_UINT32(
      MLN_GPU_SYNC_CPU_COMPLETE, capabilities.producer_sync_kind
    );
    TEST_ASSERT_BITS_LOW(
      MLN_GPU_SYNC_KIND_FLAG_VULKAN_TIMELINE_SEMAPHORE,
      capabilities.consumer_sync_kinds
    );
    mln_test_render_fixture_destroy(&fixture);
    mln_test_render_fixture fallback = {0};
    TEST_ASSERT_FALSE(mln_test_render_fixture_create_synced(
      map, &fallback, MLN_GPU_SYNC_KIND_FLAG_VULKAN_TIMELINE_SEMAPHORE
    ));
    mln_test_destroy_map(map);
    mln_test_destroy_runtime(runtime);
    return;
  }
  TEST_ASSERT_BITS_HIGH(
    MLN_GPU_SYNC_KIND_FLAG_VULKAN_TIMELINE_SEMAPHORE,
    capabilities.consumer_sync_kinds
  );
  mln_test_graphics* graphics = mln_test_render_fixture_graphics(&fixture);

  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 1)
  );
  mln_acquired_frame first = MLN_HANDLE_NULL;
  MLN_TEST_OK(mln_render_session_acquire_frame(fixture.session, &first, NULL));
  mln_gpu_sync producer = {.size = sizeof(mln_gpu_sync)};
  MLN_TEST_OK(mln_acquired_frame_get_producer_sync(first, &producer, NULL));
  TEST_ASSERT_EQUAL_UINT32(
    MLN_GPU_SYNC_VULKAN_TIMELINE_SEMAPHORE, producer.kind
  );
  TEST_ASSERT_NOT_EQUAL_UINT64(0, producer.handle);
  TEST_ASSERT_NOT_EQUAL_UINT64(0, producer.value);
  const uint64_t produced = producer.handle;
  const uint64_t first_value = producer.value;
  TEST_ASSERT_EQUAL_INT_MESSAGE(
    1,
    mln_test_graphics_vulkan_timeline_wait(
      graphics, produced, first_value, UINT64_MAX
    ),
    mln_test_graphics_last_error()
  );
  expect_texture_color(
    mln_test_render_fixture_texture(&fixture, 0), red,
    "the first timeline frame's texture"
  );

  // The host's timeline is still at zero when it releases the frame.
  const uint64_t consumed = mln_test_graphics_vulkan_timeline_create(graphics);
  TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(
    0, consumed, mln_test_graphics_last_error()
  );
  const mln_gpu_sync consumer = {
    .size = sizeof(mln_gpu_sync),
    .kind = MLN_GPU_SYNC_VULKAN_TIMELINE_SEMAPHORE,
    .handle = consumed,
    .value = 1,
  };
  MLN_TEST_OK(mln_acquired_frame_release(first, &consumer, NULL));

  paint_background_blue(runtime, map);
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 2)
  );
  mln_acquired_frame second = MLN_HANDLE_NULL;
  MLN_TEST_OK(mln_render_session_acquire_frame(fixture.session, &second, NULL));
  MLN_TEST_OK(mln_acquired_frame_get_producer_sync(second, &producer, NULL));
  TEST_ASSERT_EQUAL_UINT64(produced, producer.handle);
  TEST_ASSERT_GREATER_THAN_UINT64(first_value, producer.value);
  TEST_ASSERT_EQUAL_INT(
    0, mln_test_graphics_vulkan_timeline_wait(
         graphics, produced, producer.value, 0
       )
  );
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_graphics_vulkan_timeline_signal(graphics, consumed, 1),
    mln_test_graphics_last_error()
  );
  TEST_ASSERT_EQUAL_INT(
    1, mln_test_graphics_vulkan_timeline_wait(
         graphics, produced, producer.value, UINT64_MAX
       )
  );
  expect_texture_color(
    mln_test_render_fixture_texture(&fixture, 0), blue,
    "the reused texture after the host's timeline"
  );
  mln_test_render_release_frame(&second);
  // The host's timeline stays valid until detach completes.
  MLN_TEST_RENDER_AWAIT(
    MLN_STATUS_OK, &fixture,
    mln_render_session_detach(fixture.session, &completion.descriptor, NULL)
  );
  mln_test_graphics_vulkan_timeline_destroy(graphics, consumed);
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}
#endif

#if defined(MLN_FFI_TEST_BACKEND_VULKAN)
static void vulkan_images_go_to_their_owner_between_frames(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  mln_test_render_fixture fixture = {0};
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_render_fixture_create_vulkan_handoff_ring(
      map, &fixture, MLN_VULKAN_IMAGE_OWNER_EXTERNAL
    ),
    mln_test_graphics_last_error()
  );
  mln_test_graphics* graphics = mln_test_render_fixture_graphics(&fixture);
  for (uint64_t index = 0; index < 2; index += 1) {
    TEST_ASSERT_EQUAL_UINT32(
      MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, index + 1)
    );
    mln_acquired_frame frame = MLN_HANDLE_NULL;
    MLN_TEST_OK(
      mln_render_session_acquire_frame(fixture.session, &frame, NULL)
    );
    mln_gpu_sync producer = {.size = sizeof(mln_gpu_sync)};
    MLN_TEST_OK(mln_acquired_frame_get_producer_sync(frame, &producer, NULL));
    // A frame without a timeline completed with its release.
    if (producer.kind == MLN_GPU_SYNC_VULKAN_TIMELINE_SEMAPHORE) {
      TEST_ASSERT_EQUAL_INT(
        1, mln_test_graphics_vulkan_timeline_wait(
             graphics, producer.handle, producer.value, UINT64_MAX
           )
      );
    }
    mln_test_render_release_frame(&frame);
  }
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}
#endif

#if !defined(_WIN32) && \
  (defined(MLN_FFI_TEST_BACKEND_VULKAN) || defined(MLN_FFI_TEST_OPENGL_EGL))
// Whether the session must hand out sync files: a Vulkan device that exports
// and imports them has them, as Android's GPUs do. The Android emulator's
// Android 14 image and Mesa's software drivers, which need udmabuf for them,
// have none, and neither need an EGL display's.
static bool sync_files_required(mln_test_render_fixture* fixture) {
#if defined(MLN_FFI_TEST_BACKEND_VULKAN)
  return mln_test_graphics_vulkan_sync_files_supported(
    mln_test_render_fixture_graphics(fixture)
  );
#else
  (void)fixture;
  return false;
#endif
}

static void sync_files_order_frame_reads_and_slot_reuse(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  mln_test_render_fixture fixture = {0};
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_render_fixture_create_synced(
      map, &fixture,
      MLN_GPU_SYNC_KIND_FLAG_CPU_COMPLETE | MLN_GPU_SYNC_KIND_FLAG_SYNC_FD
    ),
    mln_test_graphics_last_error()
  );
  mln_render_session_capabilities capabilities = {
    .size = sizeof(mln_render_session_capabilities)
  };
  MLN_TEST_OK(
    mln_render_session_get_capabilities(fixture.session, &capabilities, NULL)
  );
  if (capabilities.producer_sync_kind != MLN_GPU_SYNC_SYNC_FD) {
    TEST_ASSERT_FALSE_MESSAGE(
      sync_files_required(&fixture),
      "the Vulkan device exports and imports sync files, but the session "
      "hands out none"
    );
    TEST_ASSERT_EQUAL_UINT32(
      MLN_GPU_SYNC_CPU_COMPLETE, capabilities.producer_sync_kind
    );
    mln_test_render_fixture_destroy(&fixture);
    mln_test_render_fixture fallback = {0};
    TEST_ASSERT_FALSE(mln_test_render_fixture_create_synced(
      map, &fallback, MLN_GPU_SYNC_KIND_FLAG_SYNC_FD
    ));
    mln_test_destroy_map(map);
    mln_test_destroy_runtime(runtime);
    return;
  }
  TEST_ASSERT_BITS_HIGH(
    MLN_GPU_SYNC_KIND_FLAG_SYNC_FD, capabilities.consumer_sync_kinds
  );

  const uint8_t* const colors[] = {red, blue};
  for (uint64_t index = 0; index < 2; index += 1) {
    if (index == 1) paint_background_blue(runtime, map);
    TEST_ASSERT_EQUAL_UINT32(
      MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, index + 1)
    );
    mln_acquired_frame frame = MLN_HANDLE_NULL;
    MLN_TEST_OK(
      mln_render_session_acquire_frame(fixture.session, &frame, NULL)
    );
    mln_gpu_sync producer = {.size = sizeof(mln_gpu_sync)};
    MLN_TEST_OK(mln_acquired_frame_get_producer_sync(frame, &producer, NULL));
    TEST_ASSERT_EQUAL_UINT32(MLN_GPU_SYNC_SYNC_FD, producer.kind);
    // -1 is a fence that has already signaled.
    const int produced = (int)(int64_t)producer.handle;
    TEST_ASSERT_GREATER_OR_EQUAL_INT(-1, produced);
    if (produced >= 0) {
      struct pollfd signaled = {.fd = produced, .events = POLLIN};
      TEST_ASSERT_EQUAL_INT(1, poll(&signaled, 1, 10000));
    }
    expect_texture_color(
      mln_test_render_fixture_texture(&fixture, 0), colors[index],
      "the sync file frame's texture"
    );
    mln_gpu_sync consumer = {
      .size = sizeof(mln_gpu_sync), .kind = MLN_GPU_SYNC_SYNC_FD
    };
    if (index == 0 && produced >= 0) {
      consumer.handle = (uint64_t)(int64_t)dup(produced);
      void* scope = NULL;
      MLN_TEST_OK(mln_acquired_frame_view_begin(frame, &scope, NULL));
      MLN_TEST_STATUS(
        MLN_STATUS_BUSY, mln_acquired_frame_release(frame, &consumer, NULL)
      );
      mln_acquired_frame_view_end(scope);
      TEST_ASSERT_NOT_EQUAL_INT(
        -1, fcntl((int)(int64_t)consumer.handle, F_GETFD)
      );
    } else {
      consumer.handle = (uint64_t)(int64_t)-1;
    }
    MLN_TEST_OK(mln_acquired_frame_release(frame, &consumer, NULL));
  }
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}
#endif

#if defined(_WIN32) && defined(MLN_FFI_TEST_BACKEND_VULKAN)
// No CI driver shares a timeline as a D3D12 fence: lavapipe has no Win32
// semaphores. Under Wine, winevulkan shares them over a hardware driver.
#define D3D12_FENCES_REQUIRED 0

// The host opens each fence's handle as a timeline semaphore of its own
// device, as D3D12 would open it as an ID3D12Fence.
static void d3d12_fences_order_frame_reads_and_slot_reuse(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  mln_test_render_fixture fixture = {0};
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_render_fixture_create_synced(
      map, &fixture,
      MLN_GPU_SYNC_KIND_FLAG_CPU_COMPLETE | MLN_GPU_SYNC_KIND_FLAG_D3D12_FENCE
    ),
    mln_test_graphics_last_error()
  );
  mln_render_session_capabilities capabilities = {
    .size = sizeof(mln_render_session_capabilities)
  };
  MLN_TEST_OK(
    mln_render_session_get_capabilities(fixture.session, &capabilities, NULL)
  );
  if (capabilities.producer_sync_kind != MLN_GPU_SYNC_D3D12_FENCE) {
    TEST_ASSERT_FALSE_MESSAGE(
      D3D12_FENCES_REQUIRED, "this target's driver shares no D3D12 fences"
    );
    TEST_ASSERT_EQUAL_UINT32(
      MLN_GPU_SYNC_CPU_COMPLETE, capabilities.producer_sync_kind
    );
    TEST_ASSERT_BITS_LOW(
      MLN_GPU_SYNC_KIND_FLAG_D3D12_FENCE, capabilities.consumer_sync_kinds
    );
    mln_test_render_fixture_destroy(&fixture);
    mln_test_render_fixture fallback = {0};
    TEST_ASSERT_FALSE(mln_test_render_fixture_create_synced(
      map, &fallback, MLN_GPU_SYNC_KIND_FLAG_D3D12_FENCE
    ));
    mln_test_destroy_map(map);
    mln_test_destroy_runtime(runtime);
    return;
  }
  TEST_ASSERT_BITS_HIGH(
    MLN_GPU_SYNC_KIND_FLAG_D3D12_FENCE, capabilities.consumer_sync_kinds
  );
  mln_test_graphics* graphics = mln_test_render_fixture_graphics(&fixture);

  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 1)
  );
  mln_acquired_frame first = MLN_HANDLE_NULL;
  MLN_TEST_OK(mln_render_session_acquire_frame(fixture.session, &first, NULL));
  mln_gpu_sync producer = {.size = sizeof(mln_gpu_sync)};
  MLN_TEST_OK(mln_acquired_frame_get_producer_sync(first, &producer, NULL));
  TEST_ASSERT_EQUAL_UINT32(MLN_GPU_SYNC_D3D12_FENCE, producer.kind);
  TEST_ASSERT_NOT_EQUAL_UINT64(0, producer.handle);
  TEST_ASSERT_NOT_EQUAL_UINT64(0, producer.value);
  const uint64_t produced_handle = producer.handle;
  const uint64_t first_value = producer.value;
  const uint64_t produced =
    mln_test_graphics_vulkan_d3d12_fence_import(graphics, produced_handle);
  TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(
    0, produced, mln_test_graphics_last_error()
  );
  TEST_ASSERT_EQUAL_INT_MESSAGE(
    1,
    mln_test_graphics_vulkan_timeline_wait(
      graphics, produced, first_value, UINT64_MAX
    ),
    mln_test_graphics_last_error()
  );
  expect_texture_color(
    mln_test_render_fixture_texture(&fixture, 0), red,
    "the first D3D12 fence frame's texture"
  );

  // The host's fence is still at zero when it releases the frame.
  uint64_t consumed_handle = 0;
  const uint64_t consumed =
    mln_test_graphics_vulkan_d3d12_fence_create(graphics, &consumed_handle);
  TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(
    0, consumed, mln_test_graphics_last_error()
  );
  const mln_gpu_sync consumer = {
    .size = sizeof(mln_gpu_sync),
    .kind = MLN_GPU_SYNC_D3D12_FENCE,
    .handle = consumed_handle,
    .value = 1,
  };
  MLN_TEST_OK(mln_acquired_frame_release(first, &consumer, NULL));

  paint_background_blue(runtime, map);
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 2)
  );
  mln_acquired_frame second = MLN_HANDLE_NULL;
  MLN_TEST_OK(mln_render_session_acquire_frame(fixture.session, &second, NULL));
  MLN_TEST_OK(mln_acquired_frame_get_producer_sync(second, &producer, NULL));
  TEST_ASSERT_EQUAL_UINT64(produced_handle, producer.handle);
  TEST_ASSERT_GREATER_THAN_UINT64(first_value, producer.value);
  TEST_ASSERT_EQUAL_INT(
    0, mln_test_graphics_vulkan_timeline_wait(
         graphics, produced, producer.value, 0
       )
  );
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_graphics_vulkan_timeline_signal(graphics, consumed, 1),
    mln_test_graphics_last_error()
  );
  TEST_ASSERT_EQUAL_INT(
    1, mln_test_graphics_vulkan_timeline_wait(
         graphics, produced, producer.value, UINT64_MAX
       )
  );
  expect_texture_color(
    mln_test_render_fixture_texture(&fixture, 0), blue,
    "the reused texture after the host's fence"
  );
  mln_test_render_release_frame(&second);
  // The host's fence handle stays open until detach completes.
  MLN_TEST_RENDER_AWAIT(
    MLN_STATUS_OK, &fixture,
    mln_render_session_detach(fixture.session, &completion.descriptor, NULL)
  );
  mln_test_graphics_d3d12_fence_close(consumed_handle);
  mln_test_graphics_vulkan_timeline_destroy(graphics, consumed);
  mln_test_graphics_vulkan_timeline_destroy(graphics, produced);
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}
#endif

#if defined(__APPLE__)
typedef struct event_target {
  void* event;
  uint64_t value;
} event_target;

static bool event_reached(void* context) {
  const event_target* target = context;
  return mln_test_graphics_metal_shared_event_value(target->event) >=
         target->value;
}

// Metal, MoltenVK, and ANGLE all hand frames over this way.
static void metal_shared_events_order_frame_reads_and_slot_reuse(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  mln_test_render_fixture fixture = {0};
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_render_fixture_create_synced(
      map, &fixture,
      MLN_GPU_SYNC_KIND_FLAG_CPU_COMPLETE |
        MLN_GPU_SYNC_KIND_FLAG_METAL_SHARED_EVENT
    ),
    mln_test_graphics_last_error()
  );
  mln_render_session_capabilities capabilities = {
    .size = sizeof(mln_render_session_capabilities)
  };
  MLN_TEST_OK(
    mln_render_session_get_capabilities(fixture.session, &capabilities, NULL)
  );
  TEST_ASSERT_EQUAL_UINT32(
    MLN_GPU_SYNC_METAL_SHARED_EVENT, capabilities.producer_sync_kind
  );
  TEST_ASSERT_BITS_HIGH(
    MLN_GPU_SYNC_KIND_FLAG_METAL_SHARED_EVENT, capabilities.consumer_sync_kinds
  );

  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 1)
  );
  mln_acquired_frame first = MLN_HANDLE_NULL;
  MLN_TEST_OK(mln_render_session_acquire_frame(fixture.session, &first, NULL));
  mln_gpu_sync producer = {.size = sizeof(mln_gpu_sync)};
  MLN_TEST_OK(mln_acquired_frame_get_producer_sync(first, &producer, NULL));
  TEST_ASSERT_EQUAL_UINT32(MLN_GPU_SYNC_METAL_SHARED_EVENT, producer.kind);
  TEST_ASSERT_NOT_EQUAL_UINT64(0, producer.handle);
  TEST_ASSERT_NOT_EQUAL_UINT64(0, producer.value);
  event_target produced = {
    .event = (void*)(uintptr_t)producer.handle, .value = producer.value
  };
  TEST_ASSERT_TRUE(mln_test_await(
    event_reached, &produced, mln_test_deadline_default(),
    "the first frame's event"
  ));
  expect_texture_color(
    mln_test_render_fixture_texture(&fixture, 0), red,
    "the first shared event frame's texture"
  );

  void* consumed = mln_test_graphics_metal_shared_event_create();
  TEST_ASSERT_NOT_NULL_MESSAGE(consumed, mln_test_graphics_last_error());
  const uint64_t release_value =
    mln_test_graphics_metal_shared_event_value(consumed) + 1;
  const mln_gpu_sync consumer = {
    .size = sizeof(mln_gpu_sync),
    .kind = MLN_GPU_SYNC_METAL_SHARED_EVENT,
    .handle = (uint64_t)(uintptr_t)consumed,
    .value = release_value,
  };
  MLN_TEST_OK(mln_acquired_frame_release(first, &consumer, NULL));

  paint_background_blue(runtime, map);
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 2)
  );
  mln_acquired_frame second = MLN_HANDLE_NULL;
  MLN_TEST_OK(mln_render_session_acquire_frame(fixture.session, &second, NULL));
  MLN_TEST_OK(mln_acquired_frame_get_producer_sync(second, &producer, NULL));
  TEST_ASSERT_EQUAL_UINT64(
    (uint64_t)(uintptr_t)produced.event, producer.handle
  );
  TEST_ASSERT_GREATER_THAN_UINT64(produced.value, producer.value);
  produced.value = producer.value;
  TEST_ASSERT_FALSE(event_reached(&produced));
  mln_test_graphics_metal_shared_event_signal(consumed, release_value);
  TEST_ASSERT_TRUE(mln_test_await(
    event_reached, &produced, mln_test_deadline_default(),
    "the second frame's event"
  ));
  expect_texture_color(
    mln_test_render_fixture_texture(&fixture, 0), blue,
    "the reused texture after the host's event"
  );
  mln_test_render_release_frame(&second);
  // The host's event stays valid until detach completes.
  MLN_TEST_RENDER_AWAIT(
    MLN_STATUS_OK, &fixture,
    mln_render_session_detach(fixture.session, &completion.descriptor, NULL)
  );
  mln_test_graphics_metal_shared_event_release(consumed);
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}
#endif

#if defined(MLN_FFI_TEST_BACKEND_METAL)
// Signals the host's event once the case has checked what it needs to, or
// after a few seconds, so that a render held back by the event still returns
// and the case fails instead of hanging.
static bool host_read_signaled(void* event) {
  return mln_test_graphics_metal_shared_event_value(event) != 0;
}

static bool never(void* context) {
  (void)context;
  return false;
}

// Signals the host's event after a short while, as a host whose GPU read ends.
static void signal_after_a_while(void* event) {
  (void)mln_test_await(
    never, NULL, mln_test_deadline_after(300), "a host's read in progress"
  );
  mln_test_graphics_metal_shared_event_signal(event, 1);
}

static void release_held_frame_later(void* event) {
  (void)mln_test_await(
    host_read_signaled, event, mln_test_deadline_after(5000),
    "the case's signal of the host's read"
  );
  mln_test_graphics_metal_shared_event_signal(event, 1);
}

// The CPU encodes a frame while the GPU still runs the previous one: only a
// slot's own last frame holds a frame into that slot back. Here the host keeps
// the GPU on frame 2 by releasing the slot it renders into with an event it
// has not signaled, and frame 3, into the other slot, renders anyway.
static void a_frame_renders_while_the_gpu_runs_the_frame_before(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  mln_test_render_fixture fixture = {0};
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_render_fixture_create_synced_ring(
      map, &fixture, 2, MLN_GPU_SYNC_KIND_FLAG_METAL_SHARED_EVENT
    ),
    mln_test_graphics_last_error()
  );

  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 1)
  );
  mln_acquired_frame first = MLN_HANDLE_NULL;
  MLN_TEST_OK(mln_render_session_acquire_frame(fixture.session, &first, NULL));
  uint32_t first_slot = 0;
  mln_test_frame_texture_handle(first, &first_slot);
  void* host_read = mln_test_graphics_metal_shared_event_create();
  TEST_ASSERT_NOT_NULL_MESSAGE(host_read, mln_test_graphics_last_error());
  const mln_gpu_sync consumer = {
    .size = sizeof(mln_gpu_sync),
    .kind = MLN_GPU_SYNC_METAL_SHARED_EVENT,
    .handle = (uint64_t)(uintptr_t)host_read,
    .value = 1,
  };
  MLN_TEST_OK(mln_acquired_frame_release(first, &consumer, NULL));
  mln_test_thread* releaser =
    mln_test_thread_start(release_held_frame_later, host_read);

  // Frame 2 reuses the released slot, so the GPU holds it until the host's
  // event signals.
  paint_background_blue(runtime, map);
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 2)
  );
  mln_acquired_frame second = MLN_HANDLE_NULL;
  MLN_TEST_OK(mln_render_session_acquire_frame(fixture.session, &second, NULL));
  uint32_t second_slot = 0;
  mln_test_frame_texture_handle(second, &second_slot);
  TEST_ASSERT_EQUAL_UINT32(first_slot, second_slot);
  mln_gpu_sync producer = {.size = sizeof(mln_gpu_sync)};
  MLN_TEST_OK(mln_acquired_frame_get_producer_sync(second, &producer, NULL));
  event_target held = {
    .event = (void*)(uintptr_t)producer.handle, .value = producer.value
  };

  // Frame 3 goes to the other slot, and renders while frame 2 is held.
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 3)
  );
  TEST_ASSERT_FALSE_MESSAGE(
    event_reached(&held),
    "frame 3 waited for the GPU to finish frame 2, in another slot"
  );
  mln_acquired_frame third = MLN_HANDLE_NULL;
  MLN_TEST_OK(mln_render_session_acquire_frame(fixture.session, &third, NULL));
  uint32_t third_slot = 0;
  mln_test_frame_texture_handle(third, &third_slot);
  TEST_ASSERT_NOT_EQUAL_UINT32(second_slot, third_slot);
  MLN_TEST_OK(mln_acquired_frame_get_producer_sync(third, &producer, NULL));
  event_target produced = {
    .event = (void*)(uintptr_t)producer.handle, .value = producer.value
  };

  mln_test_graphics_metal_shared_event_signal(host_read, 1);
  mln_test_thread_join(releaser);
  TEST_ASSERT_TRUE(mln_test_await(
    event_reached, &produced, mln_test_deadline_default(), "frame 3's event"
  ));
  expect_texture_color(
    mln_test_render_fixture_texture(&fixture, second_slot), blue,
    "frame 2's texture"
  );
  expect_texture_color(
    mln_test_render_fixture_texture(&fixture, third_slot), blue,
    "frame 3's texture"
  );
  mln_test_render_release_frame(&second);
  mln_test_render_release_frame(&third);
  MLN_TEST_RENDER_AWAIT(
    MLN_STATUS_OK, &fixture,
    mln_render_session_detach(fixture.session, &completion.descriptor, NULL)
  );
  mln_test_graphics_metal_shared_event_release(host_read);
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}
#endif

#if defined(MLN_FFI_TEST_BACKEND_METAL)
static event_target acquire_producer(
  const mln_test_render_fixture* fixture, mln_acquired_frame* out_frame,
  uint32_t* out_slot
) {
  MLN_TEST_OK(
    mln_render_session_acquire_frame(fixture->session, out_frame, NULL)
  );
  mln_test_frame_texture_handle(*out_frame, out_slot);
  mln_gpu_sync producer = {.size = sizeof(mln_gpu_sync)};
  MLN_TEST_OK(
    mln_acquired_frame_get_producer_sync(*out_frame, &producer, NULL)
  );
  return (event_target){
    .event = (void*)(uintptr_t)producer.handle, .value = producer.value
  };
}

// Frames into other slots no longer wait for the frame before them, so the
// host's event for a released slot must hold back the next frame into that
// slot, wherever it comes in the ring, and only that frame.
static void a_host_read_holds_back_only_the_next_frame_into_its_slot(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  mln_test_render_fixture fixture = {0};
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_render_fixture_create_synced_ring(
      map, &fixture, 2, MLN_GPU_SYNC_KIND_FLAG_METAL_SHARED_EVENT
    ),
    mln_test_graphics_last_error()
  );

  mln_acquired_frame first = MLN_HANDLE_NULL;
  mln_acquired_frame second = MLN_HANDLE_NULL;
  uint32_t first_slot = 0;
  uint32_t second_slot = 0;
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 1)
  );
  (void)acquire_producer(&fixture, &first, &first_slot);
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 2)
  );
  (void)acquire_producer(&fixture, &second, &second_slot);
  TEST_ASSERT_NOT_EQUAL_UINT32(first_slot, second_slot);

  // The host is done with the first slot, and still reads the second.
  mln_test_render_release_frame(&first);
  void* host_read = mln_test_graphics_metal_shared_event_create();
  TEST_ASSERT_NOT_NULL_MESSAGE(host_read, mln_test_graphics_last_error());
  const mln_gpu_sync consumer = {
    .size = sizeof(mln_gpu_sync),
    .kind = MLN_GPU_SYNC_METAL_SHARED_EVENT,
    .handle = (uint64_t)(uintptr_t)host_read,
    .value = 1,
  };
  MLN_TEST_OK(mln_acquired_frame_release(second, &consumer, NULL));

  // Frame 3 renders into the first slot, which nobody reads: it finishes.
  paint_background_blue(runtime, map);
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 3)
  );
  mln_acquired_frame third = MLN_HANDLE_NULL;
  uint32_t third_slot = 0;
  event_target third_done = acquire_producer(&fixture, &third, &third_slot);
  TEST_ASSERT_EQUAL_UINT32(first_slot, third_slot);
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_await(
      event_reached, &third_done, mln_test_deadline_default(),
      "frame 3, into a slot the host released"
    ),
    "the host's read of another slot held frame 3 back"
  );

  // Frame 4 renders into the slot the host still reads: it waits.
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 4)
  );
  mln_acquired_frame fourth = MLN_HANDLE_NULL;
  uint32_t fourth_slot = 0;
  event_target fourth_done = acquire_producer(&fixture, &fourth, &fourth_slot);
  TEST_ASSERT_EQUAL_UINT32(second_slot, fourth_slot);
  TEST_ASSERT_FALSE_MESSAGE(
    event_reached(&fourth_done),
    "frame 4 rendered into the slot the host still reads"
  );
  mln_test_graphics_metal_shared_event_signal(host_read, 1);
  TEST_ASSERT_TRUE(mln_test_await(
    event_reached, &fourth_done, mln_test_deadline_default(),
    "frame 4 after the host's read"
  ));
  expect_texture_color(
    mln_test_render_fixture_texture(&fixture, fourth_slot), blue,
    "frame 4's texture"
  );

  mln_test_render_release_frame(&third);
  mln_test_render_release_frame(&fourth);
  MLN_TEST_RENDER_AWAIT(
    MLN_STATUS_OK, &fixture,
    mln_render_session_detach(fixture.session, &completion.descriptor, NULL)
  );
  mln_test_graphics_metal_shared_event_release(host_read);
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}
#endif

#if defined(MLN_FFI_TEST_BACKEND_METAL)
static mln_gpu_sync shared_event_consumer(void* event) {
  return (mln_gpu_sync){
    .size = sizeof(mln_gpu_sync),
    .kind = MLN_GPU_SYNC_METAL_SHARED_EVENT,
    .handle = (uint64_t)(uintptr_t)event,
    .value = 1,
  };
}

// A retarget renumbers the slots, so a host's read of a slot of the outgoing
// ring holds back the next frame, whatever its slot in the new ring.
static void a_host_read_holds_back_the_first_frame_after_a_retarget(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  mln_test_render_fixture fixture = {0};
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_render_fixture_create_synced_ring(
      map, &fixture, 2, MLN_GPU_SYNC_KIND_FLAG_METAL_SHARED_EVENT
    ),
    mln_test_graphics_last_error()
  );
  mln_acquired_frame first = MLN_HANDLE_NULL;
  mln_acquired_frame second = MLN_HANDLE_NULL;
  uint32_t first_slot = 0;
  uint32_t second_slot = 0;
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 1)
  );
  (void)acquire_producer(&fixture, &first, &first_slot);
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 2)
  );
  event_target second_done = acquire_producer(&fixture, &second, &second_slot);
  TEST_ASSERT_TRUE(mln_test_await(
    event_reached, &second_done, mln_test_deadline_default(), "frame 2"
  ));
  // The host is done with the first slot and still reads the second, whose
  // index the new ring's first frame does not take.
  mln_test_render_release_frame(&first);
  void* host_read = mln_test_graphics_metal_shared_event_create();
  TEST_ASSERT_NOT_NULL_MESSAGE(host_read, mln_test_graphics_last_error());
  const mln_gpu_sync consumer = shared_event_consumer(host_read);
  MLN_TEST_OK(mln_acquired_frame_release(second, &consumer, NULL));

  mln_test_graphics_texture* ring[2] = {
    mln_test_render_fixture_new_texture(&fixture),
    mln_test_render_fixture_new_texture(&fixture),
  };
  TEST_ASSERT_NOT_NULL_MESSAGE(ring[0], mln_test_graphics_last_error());
  TEST_ASSERT_NOT_NULL_MESSAGE(ring[1], mln_test_graphics_last_error());
  MLN_TEST_RENDER_AWAIT(
    MLN_STATUS_OK, &fixture,
    mln_test_render_fixture_set_textures(
      &fixture, ring, 2,
      &completion.descriptor
    )
  );
  MLN_TEST_OK(mln_test_map_fence(map));

  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 3)
  );
  mln_acquired_frame third = MLN_HANDLE_NULL;
  uint32_t third_slot = 0;
  event_target third_done = acquire_producer(&fixture, &third, &third_slot);
  TEST_ASSERT_NOT_EQUAL_UINT32(second_slot, third_slot);
  // Held, it never finishes in this wait; unheld, it would well within it.
  TEST_ASSERT_FALSE_MESSAGE(
    mln_test_await(
      event_reached, &third_done, mln_test_deadline_after(1000),
      "frame 3, which the host's read holds"
    ),
    "the first frame after the retarget dropped the host's wait"
  );
  mln_test_graphics_metal_shared_event_signal(host_read, 1);
  TEST_ASSERT_TRUE(mln_test_await(
    event_reached, &third_done, mln_test_deadline_default(), "frame 3"
  ));
  mln_test_render_release_frame(&third);
  MLN_TEST_RENDER_AWAIT(
    MLN_STATUS_OK, &fixture,
    mln_render_session_detach(fixture.session, &completion.descriptor, NULL)
  );
  mln_test_graphics_metal_shared_event_release(host_read);
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

// A retarget to textures of another size, while a frame is still on the GPU,
// lets go of the outgoing ring only once that frame is done, and the next
// frame renders at the new size.
static void a_resizing_retarget_waits_for_the_frame_in_flight(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  mln_test_render_fixture fixture = {0};
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_render_fixture_create_synced_ring(
      map, &fixture, 2, MLN_GPU_SYNC_KIND_FLAG_METAL_SHARED_EVENT
    ),
    mln_test_graphics_last_error()
  );
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 1)
  );
  mln_acquired_frame first = MLN_HANDLE_NULL;
  uint32_t first_slot = 0;
  event_target first_done = acquire_producer(&fixture, &first, &first_slot);
  void* host_read = mln_test_graphics_metal_shared_event_create();
  TEST_ASSERT_NOT_NULL_MESSAGE(host_read, mln_test_graphics_last_error());
  const mln_gpu_sync consumer = shared_event_consumer(host_read);
  MLN_TEST_OK(mln_acquired_frame_release(first, &consumer, NULL));

  // Frame 2 goes into the released slot, so the GPU holds it until the
  // host's event; the event signals a little later, from another thread.
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 2)
  );
  event_target second_done = {
    .event = first_done.event, .value = first_done.value + 1
  };
  TEST_ASSERT_FALSE(event_reached(&second_done));
  mln_test_thread* releaser =
    mln_test_thread_start(signal_after_a_while, host_read);

  const uint32_t width = MLN_TEST_HOST_TARGET_SIZE / 2;
  const uint32_t height = MLN_TEST_HOST_TARGET_SIZE * 3 / 4;
  mln_test_graphics_texture* ring[2] = {
    mln_test_render_fixture_new_sized_texture(&fixture, width, height),
    mln_test_render_fixture_new_sized_texture(&fixture, width, height),
  };
  TEST_ASSERT_NOT_NULL_MESSAGE(ring[0], mln_test_graphics_last_error());
  TEST_ASSERT_NOT_NULL_MESSAGE(ring[1], mln_test_graphics_last_error());
  MLN_TEST_RENDER_AWAIT(
    MLN_STATUS_OK, &fixture,
    mln_test_render_fixture_set_textures(
      &fixture, ring, 2,
      &completion.descriptor
    )
  );
  TEST_ASSERT_TRUE_MESSAGE(
    event_reached(&second_done),
    "the retarget let go of the outgoing ring with a frame still on the GPU"
  );
  mln_test_thread_join(releaser);
  MLN_TEST_OK(mln_test_map_fence(map));

  paint_background_blue(runtime, map);
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 3)
  );
  mln_acquired_frame third = MLN_HANDLE_NULL;
  uint32_t third_slot = 0;
  event_target third_done = acquire_producer(&fixture, &third, &third_slot);
  TEST_ASSERT_TRUE(mln_test_await(
    event_reached, &third_done, mln_test_deadline_default(), "frame 3"
  ));
  TEST_ASSERT_EQUAL_UINT64(
    mln_test_texture_handle(ring[third_slot]),
    mln_test_frame_texture_handle(third, &third_slot)
  );
  mln_test_render_release_frame(&third);
  MLN_TEST_RENDER_AWAIT(
    MLN_STATUS_OK, &fixture,
    mln_render_session_detach(fixture.session, &completion.descriptor, NULL)
  );
  mln_test_graphics_metal_shared_event_release(host_read);
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}
#endif

#if defined(MLN_FFI_TEST_OPENGL_EGL)
static void borrowed_opengl_frames_support_gpu_fences(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  mln_test_render_fixture fixture = {0};
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_render_fixture_create_synced(
      map, &fixture, MLN_GPU_SYNC_KIND_FLAG_OPEN_GL_FENCE
    ),
    mln_test_graphics_last_error()
  );
  void* (*fence_sync)(unsigned, unsigned) =
    (void* (*)(unsigned, unsigned))eglGetProcAddress("glFenceSync");
  void (*wait_sync)(void*, unsigned, uint64_t) =
    (void (*)(void*, unsigned, uint64_t))eglGetProcAddress("glWaitSync");
  unsigned char (*is_sync)(void*) =
    (unsigned char (*)(void*))eglGetProcAddress("glIsSync");
  void (*flush)(void) = (void (*)(void))eglGetProcAddress("glFlush");
  void (*finish)(void) = (void (*)(void))eglGetProcAddress("glFinish");
  TEST_ASSERT_NOT_NULL(fence_sync);
  TEST_ASSERT_NOT_NULL(wait_sync);
  TEST_ASSERT_NOT_NULL(is_sync);
  TEST_ASSERT_NOT_NULL(flush);
  TEST_ASSERT_NOT_NULL(finish);

  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 1)
  );
  mln_acquired_frame first = MLN_HANDLE_NULL;
  MLN_TEST_OK(mln_render_session_acquire_frame(fixture.session, &first, NULL));
  mln_gpu_sync producer = {.size = sizeof(mln_gpu_sync)};
  MLN_TEST_OK(mln_acquired_frame_get_producer_sync(first, &producer, NULL));
  TEST_ASSERT_EQUAL_UINT32(MLN_GPU_SYNC_OPEN_GL_FENCE, producer.kind);
  TEST_ASSERT_TRUE(is_sync((void*)(uintptr_t)producer.handle));
  wait_sync((void*)(uintptr_t)producer.handle, 0, UINT64_MAX);
  uint32_t first_slot = UINT32_MAX;
  TEST_ASSERT_NOT_EQUAL_UINT64(
    0, mln_test_frame_texture_handle(first, &first_slot)
  );
  expect_texture_color(
    mln_test_render_fixture_texture(&fixture, first_slot), red,
    "the fence-protected first texture"
  );

  // An invalid fence keeps the frame handle available for a valid release.
  mln_gpu_sync consumer = {
    .size = sizeof(consumer), .kind = MLN_GPU_SYNC_OPEN_GL_FENCE
  };
  MLN_TEST_STATUS(
    MLN_STATUS_INVALID_ARGUMENT,
    mln_acquired_frame_release(first, &consumer, MLN_TEST_DIAGNOSTIC)
  );
  consumer.handle = (uint64_t)(uintptr_t)fence_sync(0x9117, 0);
  TEST_ASSERT_NOT_EQUAL_UINT64(0, consumer.handle);
  flush();
  MLN_TEST_OK(mln_acquired_frame_release(first, &consumer, NULL));
  first = MLN_HANDLE_NULL;
  MLN_TEST_RENDER_AWAIT(
    MLN_STATUS_OK, &fixture,
    mln_render_session_barrier(fixture.session, &completion.descriptor, NULL)
  );
  TEST_ASSERT_FALSE(is_sync((void*)(uintptr_t)consumer.handle));

  paint_background_blue(runtime, map);
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 2)
  );
  mln_acquired_frame second = MLN_HANDLE_NULL;
  MLN_TEST_OK(mln_render_session_acquire_frame(fixture.session, &second, NULL));
  MLN_TEST_OK(mln_acquired_frame_get_producer_sync(second, &producer, NULL));
  TEST_ASSERT_EQUAL_UINT32(MLN_GPU_SYNC_OPEN_GL_FENCE, producer.kind);
  TEST_ASSERT_TRUE(is_sync((void*)(uintptr_t)producer.handle));
  wait_sync((void*)(uintptr_t)producer.handle, 0, UINT64_MAX);
  uint32_t second_slot = UINT32_MAX;
  TEST_ASSERT_NOT_EQUAL_UINT64(
    0, mln_test_frame_texture_handle(second, &second_slot)
  );
  TEST_ASSERT_EQUAL_UINT32(first_slot, second_slot);
  expect_texture_color(
    mln_test_render_fixture_texture(&fixture, second_slot), blue,
    "the reused texture after the consumer fence"
  );
  finish();
  mln_test_render_release_frame(&second);
  MLN_TEST_RENDER_AWAIT(
    MLN_STATUS_OK, &fixture,
    mln_render_session_detach(fixture.session, &completion.descriptor, NULL)
  );
  TEST_ASSERT_FALSE(is_sync((void*)(uintptr_t)producer.handle));
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}
#endif

static void a_surface_presents_frames_and_takes_a_resize(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  mln_test_render_fixture fixture = {0};
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_render_fixture_create_surface(map, &fixture),
    mln_test_graphics_last_error()
  );
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 1)
  );
  MLN_TEST_STATUS(MLN_STATUS_UNSUPPORTED, read_back(&fixture));

  const mln_logical_size size = {.width = 48, .height = 32};
  MLN_TEST_RENDER_AWAIT(
    MLN_STATUS_OK, &fixture,
    mln_render_session_resize(
      fixture.session, size, &completion.descriptor, NULL
    )
  );
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RENDER_RESULT_RENDERED, render_frame(&fixture, 2)
  );
  const mln_render_session_snapshot snapshot = read_snapshot(&fixture);
  TEST_ASSERT_EQUAL_UINT32(48, snapshot.extent.width);
  TEST_ASSERT_EQUAL_UINT32(32, snapshot.extent.height);

  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

#endif

MLN_TEST_GROUP {
#if !defined(__EMSCRIPTEN__)
  RUN_TEST(a_borrowed_ring_hands_each_frame_to_the_host);
#if defined(MLN_FFI_TEST_BACKEND_VULKAN) || defined(MLN_FFI_TEST_BACKEND_METAL)
  RUN_TEST(queue_ordered_frames_share_the_hosts_queue);
  RUN_TEST(a_queue_ordered_ring_takes_new_textures_after_the_hosts_reads);
#endif
#if defined(MLN_FFI_TEST_BACKEND_VULKAN)
  RUN_TEST(vulkan_timelines_order_frame_reads_and_slot_reuse);
  RUN_TEST(vulkan_images_go_to_their_owner_between_frames);
#endif
#if !defined(_WIN32) && \
  (defined(MLN_FFI_TEST_BACKEND_VULKAN) || defined(MLN_FFI_TEST_OPENGL_EGL))
  RUN_TEST(sync_files_order_frame_reads_and_slot_reuse);
#endif
#if defined(_WIN32) && defined(MLN_FFI_TEST_BACKEND_VULKAN)
  RUN_TEST(d3d12_fences_order_frame_reads_and_slot_reuse);
#endif
#if defined(__APPLE__)
  RUN_TEST(metal_shared_events_order_frame_reads_and_slot_reuse);
#endif
#if defined(MLN_FFI_TEST_BACKEND_METAL)
  RUN_TEST(a_frame_renders_while_the_gpu_runs_the_frame_before);
  RUN_TEST(a_host_read_holds_back_only_the_next_frame_into_its_slot);
  RUN_TEST(a_host_read_holds_back_the_first_frame_after_a_retarget);
  RUN_TEST(a_resizing_retarget_waits_for_the_frame_in_flight);
#endif
#if defined(MLN_FFI_TEST_OPENGL_EGL)
  RUN_TEST(borrowed_opengl_frames_support_gpu_fences);
#endif
  RUN_TEST(a_surface_presents_frames_and_takes_a_resize);
#endif
}
