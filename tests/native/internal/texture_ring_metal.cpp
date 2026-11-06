// The memory a Metal texture ring holds beside its color textures.

#include <array>
#include <cstdint>

#include <mln/gfx/backend_scope.hpp>
#include <mln/gfx/context.hpp>
#include <mln/gfx/renderable.hpp>
#include <mln/gfx/renderer_backend.hpp>
#include <mln/mtl/renderable_resource.hpp>

#include <Metal/MTLRenderPass.hpp>

#include "maplibre_native_c.h"
#include "render/render_session_common.hpp"
#include "support/frames.h"
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

}  // namespace

MLN_TEST_GROUP {
  RUN_TEST(a_ring_shares_one_depth_and_stencil_pair_across_its_slots);
  RUN_TEST(a_resize_replaces_the_depth_and_stencil_pair);
  RUN_TEST(a_ring_pass_neither_loads_nor_stores_depth_and_stencil);
}
