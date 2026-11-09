// A still image and the frames that may complete it. The map hears of a
// frame through a mailbox that hands the runtime worker one message per turn,
// so no public command can be ordered after a given frame's delivery; the
// StillImageFrameHeldBack point marks it. The map can also complete an image
// before the session publishes the result of the frame that completed it,
// which the RenderFrameRendered point holds open.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

#include "internal/support/sync_points.hpp"
#include "maplibre_native_c.h"
#include "runtime/runtime.hpp"
#include "support/frames.h"
#include "support/map.h"
#include "support/terrain.h"
#include "support/resources.h"
#include "support/test_support.h"
#include "testing/sync_point.hpp"

namespace {

using mln::native_tests::SyncPoint;
using mln::native_tests::SyncPointScope;

constexpr char green_background_style_json[] =
  "{\"version\":8,\"sources\":{},\"layers\":[{\"id\":\"bg\","
  "\"type\":\"background\",\"paint\":{\"background-color\":\"#00ff00\"}}]}";

void read_center_pixel(
  const mln_test_render_fixture& fixture, std::uint8_t (&out_rgba)[4]
) {
  auto readback = mln_test_completion_readback();
  MLN_TEST_OK(mln_render_session_read_texture(
    fixture.session, &readback.descriptor, nullptr
  ));
  MLN_TEST_OK(mln_test_render_fixture_finish_operation(&fixture, &readback));
  auto image = mln_texture_readback_result{};
  TEST_ASSERT_TRUE(
    mln_test_completion_copy_value(&readback, &image, sizeof(image))
  );
  TEST_ASSERT_EQUAL_UINT32(64, image.info.width);
  TEST_ASSERT_EQUAL_UINT32(64, image.info.height);
  const auto* pixels = static_cast<const std::uint8_t*>(image.data.data);
  std::memcpy(out_rgba, pixels + (((32 * 64) + 32) * 4), 4);
  mln_test_completion_destroy(&readback);
}

// Renders one forced frame, which renders the session's latest update again.
void render_forced_frame(const mln_test_render_fixture& fixture) {
  mln_test_render_demand_forced(&fixture, 1);
  const auto batch = mln_test_render_wait_for_results(&fixture);
  const auto result = mln_test_render_batch_result(batch, 0);
  mln_render_frame_batch_release(batch);
  TEST_ASSERT_EQUAL_UINT32(MLN_RENDER_RESULT_RENDERED, result.disposition);
}

// A still image completes only with a frame that rendered the map as its
// request found it. A forced demand renders the previous still image's update
// again while the request waits behind a parked runtime worker, so the map
// hears of that fully loaded frame just after the request. MapLibre would
// complete the image from it; the map reports it as partial instead, and the
// image waits for a frame of its own update, which shows the new style.
void a_frame_of_an_older_update_does_not_complete_a_still_image() {
  const auto runtime = mln_test_create_runtime();
  auto options = mln_test_map_options();
  options.initial_extent =
    mln_logical_extent{.width = 64, .height = 64, .scale_factor = 1.0};
  options.map_mode = MLN_MAP_MODE_STATIC;
  const auto map = mln_test_create_map_with_options(runtime, &options);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  auto fixture = mln_test_render_fixture{};
  TEST_ASSERT_TRUE(mln_test_render_fixture_create_offscreen(map, &fixture));
  MLN_TEST_OK(mln_test_render_still_image(&fixture, map));
  // A static map publishes no update for the new style until a still image
  // requests one, so the session's latest update still shows red.
  mln_test_load_style_and_wait(
    map, mln_buffer_view{
           green_background_style_json, sizeof(green_background_style_json) - 1
         }
  );

  auto points = SyncPointScope{};
  // A task of the case's own, because a public completion can run on the
  // submitting thread instead when the command finishes first.
  auto parked = std::make_shared<mln_test_gate>();
  mln_test_gate_init(parked.get());
  mln::core::lease_runtime(runtime)->executor.invoke([parked] {
    mln_test_gate_park(parked.get());
  });
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_gate_wait_entered(parked.get()), "the runtime worker never parked"
  );
  auto still = mln_test_completion_default(0);
  MLN_TEST_OK(mln_map_request_still_image(map, &still.descriptor, nullptr));
  render_forced_frame(fixture);
  // The request's own demand can render the image as soon as the request
  // runs, so the held point keeps that frame's messages behind this check.
  points.hold(SyncPoint::StillImageFrameHeldBack);
  mln_test_gate_release(parked.get());

  // The request runs first, since it was queued before the frame's messages.
  TEST_ASSERT_TRUE(points.wait_for_hits(SyncPoint::StillImageFrameHeldBack, 1));
  TEST_ASSERT_FALSE(mln_test_completion_poll(&still));
  points.release(SyncPoint::StillImageFrameHeldBack);

  MLN_TEST_OK(mln_test_render_pending_still_image(&fixture, &still));
  mln_test_completion_destroy(&still);
  std::uint8_t rgba[4] = {};
  read_center_pixel(fixture, rgba);
  TEST_ASSERT_EQUAL_UINT8(0, rgba[0]);
  TEST_ASSERT_EQUAL_UINT8(255, rgba[1]);
  TEST_ASSERT_EQUAL_UINT8(0, rgba[2]);
  TEST_ASSERT_EQUAL_UINT8(255, rgba[3]);

  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

// A 1000 m plateau west of z12 column 2179, and 1600 m from there on.
void serve_two_plateaus(
  void*, const mln_resource_request_info* request, mln_resource_request handle
) {
  auto response = mln_resource_response{
    .size = sizeof(mln_resource_response),
    .status = MLN_RESOURCE_RESPONSE_STATUS_NO_CONTENT,
  };
  const auto url =
    std::string_view{
      reinterpret_cast<const char*>(request->requested_url.data),
      request->requested_url.size
    };
  const auto tile = url.find("/dem/");
  unsigned z = 0;
  unsigned x = 0;
  unsigned y = 0;
  if (
    tile != std::string_view::npos &&
    std::sscanf(
      std::string{url.substr(tile)}.c_str(), "/dem/%u/%u/%u", &z, &x, &y
    ) == 3
  ) {
    const auto high = z <= 12 && (x << (12 - z)) >= 2179;
    response.status = MLN_RESOURCE_RESPONSE_STATUS_OK;
    response.bytes =
      high ? mln_test_plateau_1600_png : mln_test_plateau_1000_png;
    response.byte_count =
      high ? mln_test_plateau_1600_png_size : mln_test_plateau_1000_png_size;
  }
  mln_test_request_answer(handle, &response);
}

constexpr char two_plateaus_style_json[] =
  "{\"version\":8,\"center\":[11.39,47.27],\"zoom\":14.5,\"pitch\":60,"
  "\"sources\":{\"dem\":{\"type\":\"raster-dem\",\"tiles\":"
  "[\"custom://dem/{z}/{x}/{y}.png\"],\"encoding\":\"mapbox\","
  "\"tileSize\":256,\"maxzoom\":12},\"ground\":{\"type\":\"geojson\","
  "\"data\":{\"type\":\"Polygon\",\"coordinates\":[[[10,46],[13,46],"
  "[13,48.5],[10,48.5],[10,46]]]}}},\"terrain\":{\"source\":\"dem\","
  "\"exaggeration\":1},\"layers\":[{\"id\":\"bg\",\"type\":"
  "\"background\",\"paint\":{\"background-color\":\"#ffffff\"}},{\"id\":"
  "\"ground\",\"type\":\"fill\",\"source\":\"ground\",\"paint\":"
  "{\"fill-color\":\"#00ff00\"}}]}";

struct HitsContext {
  const SyncPointScope* points;
};

bool followed_held_update(void* context) {
  return static_cast<HitsContext*>(context)->points->hits(
           SyncPoint::StillImageFollowedHeldUpdate
         ) >= 1;
}

void move_camera(mln_map map, double longitude, std::uint32_t extra_fields) {
  mln_camera_update update = {
    .size = sizeof(mln_camera_update),
    .camera = {
      .size = sizeof(mln_camera_options),
      .fields = MLN_CAMERA_OPTION_CENTER | extra_fields,
      .center = {.latitude = 47.27, .longitude = longitude},
      .center_altitude = 0.0,
    },
  };
  MLN_TEST_AWAIT_OK(
    mln_map_update_camera(map, &update, &completion.descriptor, nullptr)
  );
}

// The clamp moves the camera during a still image's frame while a command group
// holds the map's updates, so the image follows an update not stored yet. Ending
// the group stores it as the generation the image waits for, and the image
// completes, drawn from the ground. Before: the follow named a generation the
// store never reaches, or one the session had already rendered, and the image
// hung.
void a_still_image_follows_an_update_a_command_group_holds() {
  const mln_resource_provider provider = {
    .size = sizeof(mln_resource_provider),
    .callback = serve_two_plateaus,
    .user_data = nullptr,
  };
  const auto runtime = mln_test_create_runtime();
  MLN_TEST_OK(mln_test_set_resource_provider(runtime, &provider));
  auto options = mln_test_map_options();
  options.initial_extent =
    mln_logical_extent{.width = 64, .height = 64, .scale_factor = 1.0};
  options.map_mode = MLN_MAP_MODE_STATIC;
  const auto map = mln_test_create_map_with_options(runtime, &options);
  mln_test_load_style_and_wait(
    map, mln_buffer_view{
           two_plateaus_style_json, sizeof(two_plateaus_style_json) - 1
         }
  );
  auto fixture = mln_test_render_fixture{};
  TEST_ASSERT_TRUE(mln_test_render_fixture_create_offscreen(map, &fixture));
  const mln_map_terrain_options clamp = {
    .size = sizeof(mln_map_terrain_options),
    .fields = MLN_MAP_TERRAIN_OPTION_CENTER_CLAMPED_TO_GROUND,
    .center_clamped_to_ground = true,
  };
  MLN_TEST_AWAIT_OK(
    mln_map_set_terrain_options(map, &clamp, &completion.descriptor, nullptr)
  );

  // Two stills load both places' tiles and leave the last height at 1600 m,
  // so going back reports a new height and the tiles need no update to load.
  MLN_TEST_OK(mln_test_render_still_image(&fixture, map));
  move_camera(map, 11.60, 0);
  MLN_TEST_OK(mln_test_render_still_image(&fixture, map));
  move_camera(map, 11.39, MLN_CAMERA_OPTION_CENTER_ALTITUDE);

  auto points = SyncPointScope{};
  auto parked = std::make_shared<mln_test_gate>();
  mln_test_gate_init(parked.get());
  mln::core::lease_runtime(runtime)->executor.invoke([parked] {
    mln_test_gate_park(parked.get());
  });
  TEST_ASSERT_TRUE_MESSAGE(
    mln_test_gate_wait_entered(parked.get()), "the runtime worker never parked"
  );
  // In this order behind the parked worker: the request publishes its update,
  // and the group opens before any of the frame's messages arrive.
  auto still = mln_test_completion_default(0);
  MLN_TEST_OK(mln_map_request_still_image(map, &still.descriptor, nullptr));
  auto group = mln_test_completion_default(0);
  MLN_TEST_OK(mln_map_begin_command_group(map, &group.descriptor, nullptr));
  mln_test_gate_release(parked.get());

  auto hits = HitsContext{&points};
  MLN_TEST_OK(mln_test_render_step_until(
    &fixture, followed_held_update, &hits, mln_test_deadline_default(),
    "the still image to follow the held update"
  ));
  TEST_ASSERT_FALSE_MESSAGE(
    mln_test_completion_poll(&still),
    "the still image completed on a frame from before the clamp's move"
  );
  MLN_TEST_OK(mln_test_render_fixture_finish_operation(&fixture, &group));
  mln_test_completion_destroy(&group);

  MLN_TEST_AWAIT_OK(
    mln_map_end_command_group(map, &completion.descriptor, nullptr)
  );
  MLN_TEST_OK(mln_test_render_pending_still_image(&fixture, &still));
  mln_test_completion_destroy(&still);
  std::uint8_t rgba[4] = {};
  read_center_pixel(fixture, rgba);
  TEST_ASSERT_EQUAL_UINT8(0, rgba[0]);
  TEST_ASSERT_EQUAL_UINT8(255, rgba[1]);
  TEST_ASSERT_EQUAL_UINT8(0, rgba[2]);

  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

struct NextRequest {
  mln_map map;
  mln_test_completion* first;
  mln_test_completion* second;
  std::atomic_bool requested{false};
  bool first_completed = false;
  mln_status second_submitted = MLN_STATUS_NATIVE_ERROR;
  mln_status fence = MLN_STATUS_NATIVE_ERROR;
};

// Runs on the driver once the first image's frame has rendered and before its
// result is published. It waits for the map to complete that image, then
// requests the next one and waits until the request has run, so the next
// request arrives while the first one's demand is still outstanding. A case
// cannot assert off its own thread, so this records what it saw.
void request_the_next_image(SyncPoint point, void* context) noexcept {
  if (point != SyncPoint::RenderFrameRendered) return;
  auto& next = *static_cast<NextRequest*>(context);
  if (next.requested.exchange(true)) return;
  next.first_completed = mln_test_completion_wait(next.first, -1);
  next.second_submitted =
    mln_map_request_still_image(next.map, &next.second->descriptor, nullptr);
  // A read behind the request completes once the request has run.
  auto camera = mln_test_completion_default(sizeof(mln_camera_query_result));
  if (
    mln_map_get_camera(next.map, &camera.descriptor, nullptr) != MLN_STATUS_OK
  ) {
    camera.descriptor.release_user_data(camera.descriptor.user_data);
    mln_test_completion_destroy(&camera);
    return;
  }
  auto value = mln_camera_query_result{};
  next.fence = mln_test_completion_finish_value(&camera, &value, sizeof(value));
}

// A request that arrives while the previous request's demand is still
// outstanding, because the map completed that image before the session
// published the frame's result, gets a demand of its own and renders.
void a_request_behind_an_unfinished_demand_still_renders() {
  const auto runtime = mln_test_create_runtime();
  auto options = mln_test_map_options();
  options.initial_extent =
    mln_logical_extent{.width = 64, .height = 64, .scale_factor = 1.0};
  options.map_mode = MLN_MAP_MODE_STATIC;
  const auto map = mln_test_create_map_with_options(runtime, &options);
  mln_test_load_style_and_wait(map, mln_test_red_background_style_json);
  auto fixture = mln_test_render_fixture{};
  TEST_ASSERT_TRUE(mln_test_render_fixture_create_offscreen(map, &fixture));
  MLN_TEST_OK(mln_test_render_still_image(&fixture, map));

  auto first = mln_test_completion_default(0);
  auto second = mln_test_completion_default(0);
  auto next = NextRequest{.map = map, .first = &first, .second = &second};
  mln::testing::set_sync_point_handler(request_the_next_image, &next);
  MLN_TEST_OK(mln_map_request_still_image(map, &first.descriptor, nullptr));
  const auto status = mln_test_render_pending_still_image(&fixture, &first);
  const auto requested = next.requested.load();
  mln::testing::set_sync_point_handler(nullptr, nullptr);
  MLN_TEST_OK(status);
  TEST_ASSERT_TRUE(requested);
  TEST_ASSERT_TRUE(next.first_completed);
  MLN_TEST_OK(next.second_submitted);
  MLN_TEST_OK(next.fence);

  // A caller driver renders the next image within the same service, so the
  // wait above can drain its frame's result; this waits for its completion.
  MLN_TEST_OK(mln_test_render_fixture_finish_operation(&fixture, &second));
  mln_test_completion_destroy(&first);
  mln_test_completion_destroy(&second);
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

struct MissingImageOrder {
  mln_runtime runtime;
  mln_test_completion* barrier;
  std::shared_ptr<mln_test_gate> map_gate = std::make_shared<mln_test_gate>();
  std::atomic_bool missing{false};
  atomic_bool missing_requested = false;
  atomic_bool updated = false;
  atomic_bool announced = false;
  std::atomic_bool resumed{false};
  bool parked = false;
  bool update_arrived = false;
};

void order_missing_image_reply(SyncPoint point, void* context) noexcept {
  auto& order = *static_cast<MissingImageOrder*>(context);
  if (
    point == SyncPoint::RenderStyleImageMissing && !order.missing.exchange(true)
  ) {
    // Hold the map before the missing-image message reaches its mailbox.
    mln::core::lease_runtime(order.runtime)
      ->executor.invoke([gate = order.map_gate] {
        mln_test_gate_park(gate.get());
      });
    order.parked = mln_test_gate_wait_entered(order.map_gate.get());
    mln_test_flag_set(&order.missing_requested);
  } else if (
    point == SyncPoint::RenderUpdateSelecting &&
    mln_test_completion_poll(order.barrier) && !order.resumed.exchange(true)
  ) {
    // Deliver the reply and its map update immediately before selection.
    // Selecting after draining would consume that update before the renderer
    // has applied the reply, then wait forever for an update it already saw.
    mln_test_gate_release(order.map_gate.get());
    order.update_arrived = mln_test_wait_for_flag(&order.updated);
    // The notice point precedes delivery to the session. Fence its runtime
    // task too, so a late notice cannot rescue the old ordering's stalled
    // frame.
    mln::core::lease_runtime(order.runtime)->executor.invoke([&order] {
      mln_test_flag_set(&order.announced);
    });
    order.update_arrived =
      mln_test_wait_for_flag(&order.announced) && order.update_arrived;
  } else if (
    point == SyncPoint::RenderSessionUpdateNoticed && order.resumed.load()
  ) {
    mln_test_flag_set(&order.updated);
  }
}

void a_missing_image_reply_cannot_be_overtaken_by_its_map_update() {
  constexpr char style[] =
    "{\"version\":8,\"sources\":{\"point\":{\"type\":\"geojson\",\"data\":"
    "{\"type\":\"Point\",\"coordinates\":[0,0]}}},\"layers\":["
    "{\"id\":\"icon\",\"type\":\"symbol\",\"source\":\"point\","
    "\"layout\":{\"icon-image\":\"missing\"}}]}";
  const auto runtime = mln_test_create_runtime();
  auto options = mln_test_map_options();
  options.initial_extent =
    mln_logical_extent{.width = 64, .height = 64, .scale_factor = 1.0};
  options.map_mode = MLN_MAP_MODE_STATIC;
  const auto map = mln_test_create_map_with_options(runtime, &options);
  auto fixture = mln_test_render_fixture{};
  TEST_ASSERT_TRUE(mln_test_render_fixture_create_offscreen(map, &fixture));
  MLN_TEST_OK(mln_test_map_set_style_json(map, {style, sizeof(style) - 1}));

  auto barrier = mln_test_completion_default(0);
  auto order = MissingImageOrder{.runtime = runtime, .barrier = &barrier};
  mln_test_gate_init(order.map_gate.get());
  mln::testing::set_sync_point_handler(order_missing_image_reply, &order);
  auto still = mln_test_completion_default(0);
  MLN_TEST_OK(mln_map_request_still_image(map, &still.descriptor, nullptr));
  const auto missing_requested =
    mln_test_wait_for_flag(&order.missing_requested);
  // End the parked demand and arm another for the same pending image. The
  // map remains parked until that demand is about to select its update.
  MLN_TEST_OK(
    mln_render_session_barrier(fixture.session, &barrier.descriptor, nullptr)
  );
  MLN_TEST_OK(mln_test_render_fixture_finish_operation(&fixture, &barrier));
  const auto status = mln_test_render_pending_still_image(&fixture, &still);
  mln_test_gate_release(order.map_gate.get());
  mln::testing::set_sync_point_handler(nullptr, nullptr);
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
  mln_test_completion_destroy(&still);
  mln_test_completion_destroy(&barrier);
  TEST_ASSERT_TRUE(missing_requested);
  TEST_ASSERT_TRUE(order.parked);
  TEST_ASSERT_TRUE(order.update_arrived);
  MLN_TEST_OK(status);
}

}  // namespace

MLN_TEST_GROUP {
  RUN_TEST(a_missing_image_reply_cannot_be_overtaken_by_its_map_update);
  RUN_TEST(a_frame_of_an_older_update_does_not_complete_a_still_image);
  RUN_TEST(a_still_image_follows_an_update_a_command_group_holds);
  RUN_TEST(a_request_behind_an_unfinished_demand_still_renders);
}
