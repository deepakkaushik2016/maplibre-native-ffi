// The memory a Metal texture ring holds beside its color textures.

#include <array>
#include <cstdint>
#include <cstdlib>

#include <mln/gfx/backend_scope.hpp>
#include <mln/gfx/context.hpp>
#include <mln/gfx/renderable.hpp>
#include <mln/gfx/renderer_backend.hpp>
#include <mln/mtl/renderable_resource.hpp>

#include <Metal/MTLRenderPass.hpp>

#include "maplibre_native_c.h"
#include "mln_test_graphics.h"
#include "render/render_session_common.hpp"
#include "support/frames.h"
#include "support/host_graphics.h"
#include "support/map.h"
#include "support/test_support.h"
#include "support/wait.h"

namespace {

// The texture memory the session's renderer has allocated: its depth and
// stencil attachments and its own textures. The ring's color textures are the
// fixture's, so they are not counted.
auto texture_memory(const mln_test_render_fixture& fixture) -> int64_t {
  auto bytes = int64_t{-1};
  auto completion = mln_test_completion_default(0);
  MLN_TEST_OK(
    mln::core::enqueue_driver_operation(
      fixture.session,
      [&bytes](mln_render_session_object& session) {
        auto* backend = session.texture.backend->renderer_backend();
        if (backend == nullptr) return MLN_STATUS_INVALID_STATE;
        bytes = backend->getContext().renderingStats().memTextures;
        return MLN_STATUS_OK;
      },
      &completion.descriptor,
      mln::core::valueless_completion<&mln_render_session_reduce_memory_use>()
    )
  );
  MLN_TEST_OK(mln_test_render_fixture_finish_operation(&fixture, &completion));
  mln_test_completion_destroy(&completion);
  return bytes;
}

// Only one slot renders at a time, and no frame reads the depth or stencil
// another drew, so the slots share one pair: a 4-slot ring holds what one slot
// does. Each slot of a full-screen ring on an iPhone held its own pair,
// 18.7 MB.
void a_ring_shares_one_depth_and_stencil_pair_across_its_slots() {
  const auto runtime = mln_test_create_runtime();
  const auto map = mln_test_create_map(runtime);
  mln_test_render_prepare_map(map);
  auto fixture = mln_test_render_fixture{};
  TEST_ASSERT_TRUE(mln_test_render_fixture_create_borrowed_ring(
    map, &fixture, 4, MLN_TEST_PRESET_DRIVER
  ));

  // Each frame is held, so the next renders into another slot.
  auto frames = std::array<mln_acquired_frame, 4>{};
  frames[0] = mln_test_render_and_acquire(&fixture, 1);
  const auto one_slot = texture_memory(fixture);
  for (auto slot = std::size_t{1}; slot < frames.size(); ++slot) {
    frames[slot] = mln_test_render_and_acquire(&fixture, 1 + slot);
  }
  const auto four_slots = texture_memory(fixture);
  for (auto& frame : frames) mln_test_render_release_frame(&frame);

  // A 64x64 Depth32Float attachment, at least.
  TEST_ASSERT_GREATER_OR_EQUAL_INT64(64 * 64 * 4, one_slot);
  TEST_ASSERT_EQUAL_INT64(one_slot, four_slots);

  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

// A resize remakes the pair at the new size and drops the old one, so the ring
// never holds two.
void a_resize_replaces_the_depth_and_stencil_pair() {
  const auto runtime = mln_test_create_runtime();
  auto map_options = mln_test_map_options();
  map_options.initial_extent = {.width = 64, .height = 64, .scale_factor = 1.0};
  map_options.map_mode = MLN_MAP_MODE_STATIC;
  const auto map = mln_test_create_map_with_options(runtime, &map_options);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  auto fixture = mln_test_render_fixture{};
  TEST_ASSERT_TRUE(mln_test_render_fixture_create_offscreen(map, &fixture));
  MLN_TEST_OK(mln_test_render_still_image(&fixture, map));
  const auto before = texture_memory(fixture);

  auto resize = mln_test_completion_default(0);
  MLN_TEST_OK(mln_render_session_resize(
    fixture.session, mln_logical_size{32, 48}, &resize.descriptor,
    nullptr
  ));
  MLN_TEST_OK(mln_test_render_fixture_finish_operation(&fixture, &resize));
  mln_test_completion_destroy(&resize);
  MLN_TEST_OK(mln_test_render_still_image(&fixture, map));
  const auto after = texture_memory(fixture);

  // Depth32Float + Stencil8 is 5 bytes a pixel; the simulator's combined
  // depth-stencil counts only its depth texture, 4.
  TEST_ASSERT_TRUE(before == 64 * 64 * 5 || before == 64 * 64 * 4);
  TEST_ASSERT_EQUAL_INT64(before / (64 * 64) * 32 * 48, after);

  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

// The slots share the pair safely because no pass reads what an earlier pass
// left in it: each pass clears depth and stencil and stores neither. A load or
// a store would make one frame depend on another's attachments.
void a_ring_pass_neither_loads_nor_stores_depth_and_stencil() {
  const auto runtime = mln_test_create_runtime();
  const auto map = mln_test_create_map(runtime);
  mln_test_render_prepare_map(map);
  auto fixture = mln_test_render_fixture{};
  TEST_ASSERT_TRUE(mln_test_render_fixture_create_borrowed_ring(
    map, &fixture, 2, MLN_TEST_PRESET_DRIVER
  ));
  auto frame = mln_test_render_and_acquire(&fixture, 1);
  mln_test_render_release_frame(&frame);

  auto actions = std::array<NS::UInteger, 4>{99, 99, 99, 99};
  auto completion = mln_test_completion_default(0);
  MLN_TEST_OK(
    mln::core::enqueue_driver_operation(
      fixture.session,
      [&actions](mln_render_session_object& session) {
        auto* backend = session.texture.backend->renderer_backend();
        if (backend == nullptr) return MLN_STATUS_INVALID_STATE;
        const auto guard = mln::gfx::BackendScope{*backend};
        auto& resource = backend->getDefaultRenderable()
                           .getResource<mln::mtl::RenderableResource>();
        resource.bind();
        const auto& descriptor = resource.getRenderPassDescriptor();
        actions = {
          descriptor->depthAttachment()->loadAction(),
          descriptor->depthAttachment()->storeAction(),
          descriptor->stencilAttachment()->loadAction(),
          descriptor->stencilAttachment()->storeAction(),
        };
        resource.swap();
        return MLN_STATUS_OK;
      },
      &completion.descriptor,
      mln::core::valueless_completion<&mln_render_session_reduce_memory_use>()
    )
  );
  MLN_TEST_OK(mln_test_render_fixture_finish_operation(&fixture, &completion));
  mln_test_completion_destroy(&completion);

  TEST_ASSERT_EQUAL_UINT64(MTL::LoadActionClear, actions[0]);
  TEST_ASSERT_EQUAL_UINT64(MTL::StoreActionDontCare, actions[1]);
  TEST_ASSERT_EQUAL_UINT64(MTL::LoadActionClear, actions[2]);
  TEST_ASSERT_EQUAL_UINT64(MTL::StoreActionDontCare, actions[3]);

  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

auto event_reached(void* context) -> bool {
  const auto* sync = static_cast<const mln_gpu_sync*>(context);
  return mln_test_graphics_metal_shared_event_value(
           // The C ABI carries the event as the bit pattern of its pointer.
           // NOLINTNEXTLINE(performance-no-int-to-ptr)
           reinterpret_cast<void*>(static_cast<uintptr_t>(sync->handle))
         ) >= sync->value;
}

auto producer_of(mln_acquired_frame frame) -> mln_gpu_sync {
  auto sync = mln_gpu_sync{.size = sizeof(mln_gpu_sync)};
  MLN_TEST_OK(mln_acquired_frame_get_producer_sync(frame, &sync, nullptr));
  return sync;
}

// A render that fails after the renderable's first bind leaves an
// uncommitted command buffer in the slot. The next frame into that slot must
// start its own: reusing the stale one would skip the slot's waits, here the
// host's event for its read of the slot, and then drop them at commit.
void a_frame_after_an_aborted_one_still_waits_for_the_hosts_read() {
  const auto runtime = mln_test_create_runtime();
  const auto map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  auto fixture = mln_test_render_fixture{};
  TEST_ASSERT_TRUE(mln_test_render_fixture_create_synced_ring(
    map, &fixture, 2, MLN_GPU_SYNC_KIND_FLAG_METAL_SHARED_EVENT
  ));
  auto first = mln_test_render_and_acquire(&fixture, 1);
  auto first_done = producer_of(first);
  TEST_ASSERT_TRUE(mln_test_await(
    event_reached, &first_done, mln_test_deadline_default(), "frame 1"
  ));

  // A frame into the selected slot binds its renderable and goes no further.
  auto completion = mln_test_completion_default(0);
  MLN_TEST_OK(
    mln::core::enqueue_driver_operation(
      fixture.session,
      [](mln_render_session_object& session) {
        auto* backend = session.texture.backend->renderer_backend();
        if (backend == nullptr) return MLN_STATUS_INVALID_STATE;
        const auto guard = mln::gfx::BackendScope{*backend};
        backend->getDefaultRenderable()
          .getResource<mln::mtl::RenderableResource>()
          .bind();
        return MLN_STATUS_OK;
      },
      &completion.descriptor,
      mln::core::valueless_completion<&mln_render_session_reduce_memory_use>()
    )
  );
  MLN_TEST_OK(mln_test_render_fixture_finish_operation(&fixture, &completion));
  mln_test_completion_destroy(&completion);

  // The host releases the slot, and reads it until its event signals.
  auto* host_read = mln_test_graphics_metal_shared_event_create();
  TEST_ASSERT_NOT_NULL(host_read);
  const auto consumer = mln_gpu_sync{
    .size = sizeof(mln_gpu_sync),
    .kind = MLN_GPU_SYNC_METAL_SHARED_EVENT,
    .handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(host_read)),
    .value = 1,
  };
  auto first_slot = uint32_t{0};
  (void)mln_test_frame_texture_handle(first, &first_slot);
  MLN_TEST_OK(mln_acquired_frame_release(first, &consumer, nullptr));

  auto second = mln_test_render_and_acquire(&fixture, 2);
  auto second_slot = uint32_t{1};
  (void)mln_test_frame_texture_handle(second, &second_slot);
  TEST_ASSERT_EQUAL_UINT32(first_slot, second_slot);
  auto second_done = producer_of(second);
  // Held by the host's event, the frame never finishes in this wait; a frame
  // that skipped the wait finishes well within it.
  TEST_ASSERT_FALSE_MESSAGE(
    mln_test_await(
      event_reached, &second_done, mln_test_deadline_after(1000),
      "frame 2, which the host's event holds"
    ),
    "the frame after the aborted one rendered into the slot the host reads"
  );
  mln_test_graphics_metal_shared_event_signal(host_read, 1);
  TEST_ASSERT_TRUE(mln_test_await(
    event_reached, &second_done, mln_test_deadline_default(), "frame 2"
  ));
  mln_test_render_release_frame(&second);
  auto detach = mln_test_completion_default(0);
  MLN_TEST_OK(
    mln_render_session_detach(fixture.session, &detach.descriptor, nullptr)
  );
  MLN_TEST_OK(mln_test_render_fixture_finish_operation(&fixture, &detach));
  mln_test_completion_destroy(&detach);
  mln_test_graphics_metal_shared_event_release(host_read);
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

}  // namespace

MLN_TEST_GROUP {
  RUN_TEST(a_ring_shares_one_depth_and_stencil_pair_across_its_slots);
  RUN_TEST(a_resize_replaces_the_depth_and_stencil_pair);
  RUN_TEST(a_ring_pass_neither_loads_nor_stores_depth_and_stencil);
  RUN_TEST(a_frame_after_an_aborted_one_still_waits_for_the_hosts_read);
}
