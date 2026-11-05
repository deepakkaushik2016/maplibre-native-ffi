// Gestures over terrain: the ground a finger grabs stays under it.

#include <math.h>

#include "support/frames.h"
#include "support/map.h"
#include "support/resources.h"
#include "support/terrain.h"
#include "support/test_support.h"

// The phone's view at pitch 60 from 3 km south of a crest of the ridge
// fixture, looking north over it.
static const char ridge_style_format[] =
  "{\"version\":8,\"center\":[11.39,%.6f],\"zoom\":13,\"pitch\":60,"
  "\"sources\":{\"dem\":{\"type\":\"raster-dem\",\"tiles\":"
  "[\"custom://dem/{z}/{x}/{y}.png\"],\"encoding\":\"mapbox\","
  "\"tileSize\":256,\"maxzoom\":11}},\"terrain\":{\"source\":\"dem\"},"
  "\"layers\":[{\"id\":\"bg\",\"type\":\"background\",\"paint\":"
  "{\"background-color\":\"#ffffff\"}}]}";
static const double crest_latitude = 47.21956811231548;

// A finger up the screen and left of the center, on the ridge's southern face,
// where the ground is a few hundred meters higher than the center's.
static const mln_screen_point on_the_face = {.x = 120, .y = 300};

typedef struct ridge_view {
  mln_test_dem_tile tile;
  mln_runtime runtime;
  mln_map map;
  mln_test_render_fixture fixture;
} ridge_view;

// A continuous map of the ridge, settled. With `clamped`, the camera's center
// keeps to the ground, as the app's map does. `event_mask` selects the events
// the map queues.
static void open_ridge_view(
  ridge_view* view, bool clamped, mln_runtime_event_flags event_mask
) {
  view->tile = (mln_test_dem_tile){
    .bytes = mln_test_ridge_png, .size = mln_test_ridge_png_size
  };
  const mln_resource_provider provider = mln_test_dem_provider(&view->tile);
  view->runtime = mln_test_create_runtime();
  MLN_TEST_OK(mln_test_set_resource_provider(view->runtime, &provider));
  mln_map_options options = mln_test_map_options();
  options.map_mode = MLN_MAP_MODE_CONTINUOUS;
  options.fields |= MLN_MAP_OPTION_EVENT_MASK;
  options.event_mask = event_mask;
  view->map = mln_test_create_map_with_options(view->runtime, &options);
  const mln_map_terrain_options terrain = {
    .size = sizeof(mln_map_terrain_options),
    .fields = MLN_MAP_TERRAIN_OPTION_CENTER_CLAMPED_TO_GROUND,
    .center_clamped_to_ground = clamped,
  };
  MLN_TEST_AWAIT_OK(mln_map_set_terrain_options(
    view->map, &terrain, &completion.descriptor, NULL
  ));
  char style[sizeof(ridge_style_format) + 32];
  const int written = snprintf(
    style, sizeof(style), ridge_style_format, crest_latitude - 3.0 / 111.32
  );
  TEST_ASSERT_TRUE(written > 0 && (size_t)written < sizeof(style));
  mln_test_load_style_and_wait(view->map, mln_buffer_view_from_cstr(style));
  TEST_ASSERT_TRUE(
    mln_test_render_fixture_create_offscreen(view->map, &view->fixture)
  );
  const mln_logical_size phone = {.width = 390, .height = 844};
  MLN_TEST_RENDER_AWAIT(
    MLN_STATUS_OK, &view->fixture,
    mln_render_session_resize(
      view->fixture.session, phone, &completion.descriptor, NULL
    )
  );
  mln_test_render_until_idle(view->runtime, view->map, &view->fixture);
}

static void close_ridge_view(ridge_view* view) {
  mln_test_render_fixture_destroy(&view->fixture);
  mln_test_destroy_map(view->map);
  mln_test_destroy_runtime(view->runtime);
}

// Renders one frame and waits until the map has taken in what the frame
// reported, such as the grabbed ground's height.
static void render_frame(const ridge_view* view) {
  mln_frame_demand demand = {.size = sizeof(mln_frame_demand)};
  MLN_TEST_OK(
    mln_render_session_demand_frame(view->fixture.session, &demand, NULL)
  );
  mln_render_frame_batch_release(
    mln_test_render_wait_for_results(&view->fixture)
  );
  MLN_TEST_OK(mln_test_map_fence(view->map));
}

static void apply_delta(const ridge_view* view, const mln_camera_delta* delta) {
  MLN_TEST_AWAIT_OK(
    mln_map_apply_camera_delta(view->map, delta, &completion.descriptor, NULL)
  );
}

// Puts a finger down at `finger`: the gesture begins with an anchored pan that
// doesn't move, which grabs the ground there.
static void touch_down(const ridge_view* view, mln_screen_point finger) {
  const mln_camera_delta down = {
    .size = sizeof(mln_camera_delta),
    .fields = MLN_CAMERA_DELTA_OFFSET | MLN_CAMERA_DELTA_ANCHOR,
    .anchor = finger,
    .gesture_phase = MLN_GESTURE_PHASE_BEGIN,
  };
  apply_delta(view, &down);
}

static void lift(const ridge_view* view) {
  const mln_camera_delta up = {
    .size = sizeof(mln_camera_delta),
    .gesture_phase = MLN_GESTURE_PHASE_END,
  };
  apply_delta(view, &up);
}

static mln_lat_lng ground_under(
  const ridge_view* view, mln_screen_point point
) {
  mln_test_completion completion =
    mln_test_completion_default(sizeof(mln_terrain_lat_lng));
  MLN_TEST_OK(mln_render_session_query_lat_lng_for_pixel(
    view->fixture.session, point, &completion.descriptor, MLN_TEST_DIAGNOSTIC
  ));
  MLN_TEST_OK(
    mln_test_render_fixture_finish_operation(&view->fixture, &completion)
  );
  TEST_ASSERT_EQUAL_size_t_MESSAGE(
    1, mln_test_completion_value_count(&completion), "no ground there"
  );
  mln_terrain_lat_lng ground = {0};
  TEST_ASSERT_TRUE(
    mln_test_completion_copy_value(&completion, &ground, sizeof(ground))
  );
  mln_test_completion_destroy(&completion);
  return ground.coordinate;
}

// How far from `finger` the last frame drew `ground`, in pixels.
static double drawn_distance(
  const ridge_view* view, mln_lat_lng ground, mln_screen_point finger
) {
  mln_test_completion completion =
    mln_test_completion_default(sizeof(mln_terrain_screen_point));
  MLN_TEST_OK(mln_render_session_query_pixel_for_lat_lng(
    view->fixture.session, ground, &completion.descriptor, MLN_TEST_DIAGNOSTIC
  ));
  MLN_TEST_OK(
    mln_test_render_fixture_finish_operation(&view->fixture, &completion)
  );
  TEST_ASSERT_EQUAL_size_t_MESSAGE(
    1, mln_test_completion_value_count(&completion), "the ground isn't drawn"
  );
  mln_terrain_screen_point drawn = {0};
  TEST_ASSERT_TRUE(
    mln_test_completion_copy_value(&completion, &drawn, sizeof(drawn))
  );
  mln_test_completion_destroy(&completion);
  return hypot(drawn.point.x - finger.x, drawn.point.y - finger.y);
}

// A one-finger drag up the screen and to the right, in 40 moves with a frame
// after each, as a gesture layer sends it. Every frame draws the ground the
// finger grabbed on the ridge's face under the finger. That ground is higher
// than the center: moved at the center's height instead, until the map learns
// its own, it slides off the finger.
static void a_drag_keeps_the_ground_under_the_finger(void) {
  ridge_view view = {0};
  open_ridge_view(&view, true, MLN_RUNTIME_EVENT_FLAG_NONE);
  mln_screen_point finger = on_the_face;
  touch_down(&view, finger);
  render_frame(&view);
  const mln_lat_lng grabbed = ground_under(&view, finger);

  double farthest = 0.0;
  const mln_screen_point step = {.x = 2.0, .y = -6.0};
  for (int move = 0; move < 40; ++move) {
    finger = (mln_screen_point){.x = finger.x + step.x, .y = finger.y + step.y};
    const mln_camera_delta drag = {
      .size = sizeof(mln_camera_delta),
      .fields = MLN_CAMERA_DELTA_OFFSET | MLN_CAMERA_DELTA_ANCHOR,
      .offset = step,
      .anchor = finger,
      .gesture_phase = MLN_GESTURE_PHASE_UPDATE,
    };
    apply_delta(&view, &drag);
    render_frame(&view);
    farthest = fmax(farthest, drawn_distance(&view, grabbed, finger));
  }
  lift(&view);
  mln_test_render_until_idle(view.runtime, view.map, &view.fixture);

  TEST_ASSERT_LESS_THAN_DOUBLE_MESSAGE(
    2.0, farthest, "the grabbed ground left the finger"
  );
  TEST_ASSERT_LESS_THAN_DOUBLE_MESSAGE(
    2.0, drawn_distance(&view, grabbed, finger),
    "the ground left the finger after it lifted"
  );
  close_ridge_view(&view);
}

typedef struct grab_events {
  size_t count;
  double elevation_meters;
} grab_events;

static void count_grab(const mln_runtime_event* event, void* context) {
  grab_events* events = context;
  if (event->type != MLN_RUNTIME_EVENT_MAP_TERRAIN_GRABBED_GROUND) return;
  TEST_ASSERT_EQUAL_UINT32(
    MLN_RUNTIME_EVENT_PAYLOAD_TERRAIN_GRABBED_GROUND, event->payload_type
  );
  events->count += 1;
  events->elevation_meters =
    event->payload.terrain_grabbed_ground.elevation_meters;
}

static grab_events drain_grabs(const ridge_view* view) {
  grab_events events = {0};
  MLN_TEST_OK(mln_test_dispatch_events(view->runtime, count_grab, &events));
  return events;
}

static double elevation_at(const ridge_view* view, mln_lat_lng point) {
  mln_test_completion completion =
    mln_test_completion_default(sizeof(mln_terrain_elevation));
  MLN_TEST_OK(mln_render_session_query_terrain_elevation(
    view->fixture.session, point, &completion.descriptor, MLN_TEST_DIAGNOSTIC
  ));
  MLN_TEST_OK(
    mln_test_render_fixture_finish_operation(&view->fixture, &completion)
  );
  mln_terrain_elevation elevation = {0};
  TEST_ASSERT_TRUE(
    mln_test_completion_copy_value(&completion, &elevation, sizeof(elevation))
  );
  mln_test_completion_destroy(&completion);
  return elevation.meters;
}

// The frame after a grab reports the grabbed ground's height, once: the moves
// that carry on with the same ground report nothing more. A grab of the same
// ground along the same line of sight finds the same height and reports
// nothing new; a grab of other ground does.
static void a_grab_reports_its_ground_height_once(void) {
  ridge_view view = {0};
  open_ridge_view(
    &view, false, MLN_RUNTIME_EVENT_FLAG_MAP_TERRAIN_GRABBED_GROUND
  );
  drain_grabs(&view);
  touch_down(&view, on_the_face);
  render_frame(&view);
  grab_events events = drain_grabs(&view);
  TEST_ASSERT_EQUAL_size_t_MESSAGE(1, events.count, "the first grab");
  const double ground = elevation_at(&view, ground_under(&view, on_the_face));
  TEST_ASSERT_DOUBLE_WITHIN(2.0, ground, events.elevation_meters);
  TEST_ASSERT_GREATER_THAN_DOUBLE(600.0, events.elevation_meters);

  // The finger holds still, then lifts; the camera stays where it is.
  for (int frame = 0; frame < 3; ++frame) {
    render_frame(&view);
  }
  lift(&view);
  render_frame(&view);
  TEST_ASSERT_EQUAL_size_t_MESSAGE(
    0, drain_grabs(&view).count, "frames after the grab"
  );

  touch_down(&view, on_the_face);
  render_frame(&view);
  TEST_ASSERT_EQUAL_size_t_MESSAGE(
    0, drain_grabs(&view).count, "the same ground again"
  );
  lift(&view);

  touch_down(&view, (mln_screen_point){.x = 300, .y = 760});
  render_frame(&view);
  TEST_ASSERT_EQUAL_size_t_MESSAGE(1, drain_grabs(&view).count, "other ground");
  lift(&view);
  close_ridge_view(&view);
}

MLN_TEST_GROUP {
  RUN_TEST(a_drag_keeps_the_ground_under_the_finger);
  RUN_TEST(a_grab_reports_its_ground_height_once);
}
