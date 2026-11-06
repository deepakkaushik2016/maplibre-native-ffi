// The memory a Metal texture ring holds beside its color textures.

#include <array>
#include <cstdint>

#include <mln/gfx/context.hpp>
#include <mln/gfx/renderer_backend.hpp>

#include "maplibre_native_c.h"
#include "render/render_session_common.hpp"
#include "support/frames.h"
#include "support/map.h"
#include "support/test_support.h"

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

}  // namespace

MLN_TEST_GROUP {
  RUN_TEST(a_ring_shares_one_depth_and_stencil_pair_across_its_slots);
}
