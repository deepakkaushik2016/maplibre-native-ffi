// The conversions between screen points and the ground that a render session's
// last frame drew.

#include <math.h>

#include "support/map.h"
#include "support/resources.h"
#include "support/terrain.h"
#include "support/test_support.h"

// A crest of the ridge fixture at z11, and the ground 3 km south of it, from
// where the phone's view looks north over the crest.
static const double crest_latitude = 47.21956811231548;
static const double longitude = 11.39;
static const double kilometer = 1.0 / 111.32;

// A static phone-sized map over the ridge, with the center 3 km south of the
// crest and the camera looking north at `pitch`.
static const char ridge_style_format[] =
  "{\"version\":8,\"center\":[%.6f,%.6f],\"zoom\":13,\"pitch\":%.1f,"
  "\"sources\":{\"dem\":{\"type\":\"raster-dem\",\"tiles\":"
  "[\"custom://dem/{z}/{x}/{y}.png\"],\"encoding\":\"mapbox\","
  "\"tileSize\":256,\"maxzoom\":11}},\"terrain\":{\"source\":\"dem\"},"
  "\"layers\":[{\"id\":\"bg\",\"type\":\"background\",\"paint\":"
  "{\"background-color\":\"#ffffff\"}}]}";

typedef struct ridge_map {
  mln_test_dem_tile tile;
  mln_runtime runtime;
  mln_map map;
  mln_test_render_fixture fixture;
} ridge_map;

static void open_ridge_map(ridge_map* view, double pitch) {
  view->tile = (mln_test_dem_tile){
    .bytes = mln_test_ridge_png, .size = mln_test_ridge_png_size
  };
  const mln_resource_provider provider = mln_test_dem_provider(&view->tile);
  view->runtime = mln_test_create_runtime();
  MLN_TEST_OK(mln_test_set_resource_provider(view->runtime, &provider));
  mln_map_options options = mln_test_map_options();
  options.map_mode = MLN_MAP_MODE_STATIC;
  view->map = mln_test_create_map_with_options(view->runtime, &options);
  const mln_bound_options bounds = {
    .size = sizeof(mln_bound_options),
    .fields = MLN_BOUND_OPTION_MAX_PITCH,
    .max_pitch = 85.0,
  };
  MLN_TEST_AWAIT_OK(
    mln_map_set_bounds(view->map, &bounds, &completion.descriptor, NULL)
  );
  char style[sizeof(ridge_style_format) + 64];
  const int written = snprintf(
    style, sizeof(style), ridge_style_format, longitude,
    crest_latitude - 3.0 * kilometer, pitch
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
}

static void close_ridge_map(ridge_map* view) {
  mln_test_render_fixture_destroy(&view->fixture);
  mln_test_destroy_map(view->map);
  mln_test_destroy_runtime(view->runtime);
}

// Finishes a submitted query whose completion copies `size` bytes to `out`,
// and returns its status. `*out_found` reports whether it delivered a value.
static mln_status finish_query(
  const mln_test_render_fixture* fixture, mln_test_completion* completion,
  mln_status submitted, void* out, size_t size, bool* out_found
) {
  *out_found = false;
  if (submitted != MLN_STATUS_OK) {
    completion->descriptor.release_user_data(completion->descriptor.user_data);
    mln_test_completion_destroy(completion);
    return submitted;
  }
  mln_status status =
    mln_test_render_fixture_finish_operation(fixture, completion);
  *out_found =
    status == MLN_STATUS_OK && mln_test_completion_value_count(completion) != 0;
  if (*out_found && !mln_test_completion_copy_value(completion, out, size)) {
    status = MLN_STATUS_NATIVE_ERROR;
  }
  mln_test_completion_destroy(completion);
  return status;
}

static mln_status query_lat_lng(
  const mln_test_render_fixture* fixture, mln_screen_point point,
  mln_terrain_lat_lng* out, bool* out_found
) {
  mln_test_completion completion =
    mln_test_completion_default(sizeof(mln_terrain_lat_lng));
  const mln_status submitted = mln_render_session_query_lat_lng_for_pixel(
    fixture->session, point, &completion.descriptor, MLN_TEST_DIAGNOSTIC
  );
  return finish_query(
    fixture, &completion, submitted, out, sizeof(*out), out_found
  );
}

static mln_status query_pixel(
  const mln_test_render_fixture* fixture, mln_lat_lng coordinate,
  mln_terrain_screen_point* out, bool* out_found
) {
  mln_test_completion completion =
    mln_test_completion_default(sizeof(mln_terrain_screen_point));
  const mln_status submitted = mln_render_session_query_pixel_for_lat_lng(
    fixture->session, coordinate, &completion.descriptor, MLN_TEST_DIAGNOSTIC
  );
  return finish_query(
    fixture, &completion, submitted, out, sizeof(*out), out_found
  );
}

static mln_status query_elevation(
  const mln_test_render_fixture* fixture, mln_lat_lng point, double* out,
  bool* out_found
) {
  mln_test_completion completion =
    mln_test_completion_default(sizeof(mln_terrain_elevation));
  const mln_status submitted = mln_render_session_query_terrain_elevation(
    fixture->session, point, &completion.descriptor, MLN_TEST_DIAGNOSTIC
  );
  mln_terrain_elevation elevation = {0};
  const mln_status status = finish_query(
    fixture, &completion, submitted, &elevation, sizeof(elevation), out_found
  );
  *out = elevation.meters;
  return status;
}

static mln_lat_lng expect_ground(
  const mln_test_render_fixture* fixture, mln_screen_point point
) {
  mln_terrain_lat_lng ground = {0};
  bool found = false;
  MLN_TEST_OK(query_lat_lng(fixture, point, &ground, &found));
  TEST_ASSERT_TRUE_MESSAGE(found, "the query found no ground");
  return ground.coordinate;
}

static mln_terrain_screen_point expect_pixel(
  const mln_test_render_fixture* fixture, mln_lat_lng coordinate
) {
  mln_terrain_screen_point pixel = {0};
  bool found = false;
  MLN_TEST_OK(query_pixel(fixture, coordinate, &pixel, &found));
  TEST_ASSERT_TRUE_MESSAGE(found, "the query found no pixel");
  return pixel;
}

static double meters_between(mln_lat_lng a, mln_lat_lng b) {
  const double north = (a.latitude - b.latitude) * 111320.0;
  const double east = (a.longitude - b.longitude) * 111320.0 *
                      cos(a.latitude * 3.14159265358979323846 / 180.0);
  return hypot(north, east);
}

// A projection of the map's view, which the still frame just drew.
static mln_map_projection project_view(mln_map map) {
  MLN_TEST_OK(mln_test_map_fence(map));
  mln_map_snapshot snapshot = {.size = sizeof(mln_map_snapshot)};
  MLN_TEST_OK(mln_map_get_snapshot(map, &snapshot, NULL));
  mln_map_projection projection = MLN_HANDLE_NULL;
  MLN_TEST_OK(
    mln_map_projection_create(&snapshot.view, &projection, MLN_TEST_DIAGNOSTIC)
  );
  return projection;
}

static void set_terrain(mln_map map, const char* json) {
  MLN_TEST_AWAIT_OK(mln_map_set_style_terrain_json(
    map, mln_buffer_view_from_cstr(json), &completion.descriptor, NULL
  ));
}

// Fingers over the lower two thirds of the screen, where the view at pitch 60
// shows the ground.
static const mln_screen_point fingers[] = {
  {.x = 40, .y = 300}, {.x = 195, .y = 300}, {.x = 350, .y = 300},
  {.x = 40, .y = 560}, {.x = 195, .y = 560}, {.x = 350, .y = 560},
  {.x = 40, .y = 820}, {.x = 195, .y = 820}, {.x = 350, .y = 820},
};

// The ground under a finger is drawn back under that finger, on the terrain
// as the frame drew it: the finger's line of sight meets the ground at the
// height that the elevation query reads there. At sea level it would meet the
// ground kilometers further on, so the answer is not the flat map's.
static void a_finger_round_trips_through_the_ground_under_it(void) {
  ridge_map view = {0};
  open_ridge_map(&view, 60.0);
  MLN_TEST_OK(mln_test_render_still_image(&view.fixture, view.map));
  const mln_map_projection projection = project_view(view.map);
  for (size_t index = 0; index < sizeof(fingers) / sizeof(fingers[0]);
       ++index) {
    const mln_screen_point finger = fingers[index];
    const mln_lat_lng ground = expect_ground(&view.fixture, finger);
    const mln_terrain_screen_point drawn = expect_pixel(&view.fixture, ground);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, finger.x, drawn.point.x);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, finger.y, drawn.point.y);
    TEST_ASSERT_FALSE_MESSAGE(drawn.occluded, "the ground is hidden");

    double meters = NAN;
    bool found = false;
    MLN_TEST_OK(query_elevation(&view.fixture, ground, &meters, &found));
    TEST_ASSERT_TRUE_MESSAGE(found, "the ground has no height");
    TEST_ASSERT_GREATER_THAN_DOUBLE(499.0, meters);
    mln_lat_lng sea_level = {0};
    MLN_TEST_OK(
      mln_map_projection_lat_lng_for_pixel(projection, finger, &sea_level, NULL)
    );
    TEST_ASSERT_GREATER_THAN_DOUBLE_MESSAGE(
      300.0, meters_between(ground, sea_level),
      "the ground is where the flat map has it"
    );
  }
  mln_map_projection_release(projection);
  close_ridge_map(&view);
}

// Seen at pitch 75 from 3 km south of the crest, the valley floor 3 km north
// of the crest lies behind it: the crest is drawn in front and hides it. The
// ridge's southern face, 1 km short of the crest, is in plain view.
static void ground_behind_the_crest_is_occluded(void) {
  ridge_map view = {0};
  open_ridge_map(&view, 75.0);
  MLN_TEST_OK(mln_test_render_still_image(&view.fixture, view.map));
  const mln_lat_lng behind = {
    .latitude = crest_latitude + 3.0 * kilometer, .longitude = longitude
  };
  const mln_lat_lng face = {
    .latitude = crest_latitude - 1.0 * kilometer, .longitude = longitude
  };
  const mln_terrain_screen_point hidden = expect_pixel(&view.fixture, behind);
  TEST_ASSERT_TRUE_MESSAGE(hidden.occluded, "the crest hides nothing");
  // What the finger at the hidden point's pixel touches is the crest.
  const mln_lat_lng touched = expect_ground(&view.fixture, hidden.point);
  TEST_ASSERT_LESS_THAN_DOUBLE(
    1500.0, meters_between(touched, (mln_lat_lng){crest_latitude, longitude})
  );
  TEST_ASSERT_FALSE_MESSAGE(
    expect_pixel(&view.fixture, face).occluded, "the near face is hidden"
  );
  close_ridge_map(&view);
}

// Above the horizon a finger touches only sky, and a point that isn't finite
// touches nothing. Neither is an error.
static void the_sky_has_no_ground(void) {
  ridge_map view = {0};
  open_ridge_map(&view, 75.0);
  MLN_TEST_OK(mln_test_render_still_image(&view.fixture, view.map));
  const mln_screen_point nowhere[] = {
    {.x = 195, .y = 5},
    {.x = 10, .y = 20},
    {.x = (double)NAN, .y = 400},
    {.x = 195, .y = (double)INFINITY},
  };
  for (size_t index = 0; index < sizeof(nowhere) / sizeof(nowhere[0]);
       ++index) {
    mln_terrain_lat_lng ground = {0};
    bool found = true;
    MLN_TEST_OK(query_lat_lng(&view.fixture, nowhere[index], &ground, &found));
    TEST_ASSERT_FALSE_MESSAGE(found, "the query found ground");
  }
  close_ridge_map(&view);
}

// Ground that no loaded DEM tile covers has no pixel: half a world away, the
// view loads none.
static void ground_without_a_loaded_dem_tile_has_no_pixel(void) {
  ridge_map view = {0};
  open_ridge_map(&view, 60.0);
  MLN_TEST_OK(mln_test_render_still_image(&view.fixture, view.map));
  mln_terrain_screen_point pixel = {0};
  bool found = true;
  MLN_TEST_OK(query_pixel(
    &view.fixture, (mln_lat_lng){.latitude = -33.86, .longitude = -168.79},
    &pixel, &found
  ));
  TEST_ASSERT_FALSE_MESSAGE(found, "the query found a pixel");
  close_ridge_map(&view);
}

// The largest exaggeration a float holds lifts the ridge past what a float can
// hold, and a point at no finite height is drawn at no finite pixel.
static void ground_lifted_past_a_float_has_no_pixel(void) {
  ridge_map view = {0};
  open_ridge_map(&view, 60.0);
  set_terrain(
    view.map, "{\"source\":\"dem\",\"exaggeration\":3.4028234663852886e38}"
  );
  MLN_TEST_OK(mln_test_render_still_image(&view.fixture, view.map));
  mln_terrain_screen_point pixel = {0};
  bool found = true;
  MLN_TEST_OK(query_pixel(
    &view.fixture,
    (mln_lat_lng){.latitude = crest_latitude, .longitude = longitude}, &pixel,
    &found
  ));
  TEST_ASSERT_FALSE_MESSAGE(found, "the query found a pixel");
  close_ridge_map(&view);
}

// Without terrain, both answers are the flat map's, as a projection of the
// frame's view finds them, and nothing is hidden.
static void without_terrain_the_answers_are_at_sea_level(void) {
  ridge_map view = {0};
  open_ridge_map(&view, 60.0);
  set_terrain(view.map, "null");
  MLN_TEST_OK(mln_test_render_still_image(&view.fixture, view.map));
  const mln_map_projection projection = project_view(view.map);
  for (size_t index = 0; index < sizeof(fingers) / sizeof(fingers[0]);
       ++index) {
    const mln_screen_point finger = fingers[index];
    mln_lat_lng sea_level = {0};
    MLN_TEST_OK(
      mln_map_projection_lat_lng_for_pixel(projection, finger, &sea_level, NULL)
    );
    const mln_lat_lng ground = expect_ground(&view.fixture, finger);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, sea_level.latitude, ground.latitude);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, sea_level.longitude, ground.longitude);

    mln_screen_point flat = {0};
    MLN_TEST_OK(
      mln_map_projection_pixel_for_lat_lng(projection, ground, &flat, NULL)
    );
    const mln_terrain_screen_point drawn = expect_pixel(&view.fixture, ground);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, flat.x, drawn.point.x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, flat.y, drawn.point.y);
    TEST_ASSERT_FALSE(drawn.occluded);
  }
  mln_map_projection_release(projection);
  close_ridge_map(&view);
}

// A session creates its renderer for its first frame, so before that both
// queries fail as every other render session query does.
static void a_session_that_never_rendered_has_no_renderer(void) {
  ridge_map view = {0};
  open_ridge_map(&view, 60.0);
  mln_terrain_lat_lng ground = {0};
  mln_terrain_screen_point pixel = {0};
  bool found = false;
  MLN_TEST_INVALID_STATE(query_lat_lng(
    &view.fixture, (mln_screen_point){.x = 195, .y = 600}, &ground, &found
  ));
  MLN_TEST_INVALID_STATE(query_pixel(
    &view.fixture,
    (mln_lat_lng){.latitude = crest_latitude, .longitude = longitude}, &pixel,
    &found
  ));
  close_ridge_map(&view);
}

typedef struct pixel_call {
  mln_lat_lng coordinate;
  bool null_completion;
} pixel_call;

static void nan_latitude(void* call) {
  ((pixel_call*)call)->coordinate.latitude = (double)NAN;
}
static void latitude_past_the_pole(void* call) {
  ((pixel_call*)call)->coordinate.latitude = 90.5;
}
static void infinite_longitude(void* call) {
  ((pixel_call*)call)->coordinate.longitude = (double)INFINITY;
}
static void longitude_past_the_antimeridian(void* call) {
  ((pixel_call*)call)->coordinate.longitude = 191.39;
}
static void pixel_without_completion(void* call) {
  ((pixel_call*)call)->null_completion = true;
}

static mln_status submit_pixel(
  void* context, const void* descriptor, mln_diagnostic* diagnostic
) {
  const mln_render_session session = *(const mln_render_session*)context;
  const pixel_call* call = descriptor;
  mln_completion completion = mln_test_discard_completion();
  return mln_render_session_query_pixel_for_lat_lng(
    session, call->coordinate, call->null_completion ? NULL : &completion,
    diagnostic
  );
}

typedef struct lat_lng_call {
  bool null_completion;
} lat_lng_call;

static void lat_lng_without_completion(void* call) {
  ((lat_lng_call*)call)->null_completion = true;
}

static mln_status submit_lat_lng(
  void* context, const void* descriptor, mln_diagnostic* diagnostic
) {
  const mln_render_session session = *(const mln_render_session*)context;
  const lat_lng_call* call = descriptor;
  mln_completion completion = mln_test_discard_completion();
  return mln_render_session_query_lat_lng_for_pixel(
    session, (mln_screen_point){.x = 195, .y = 600},
    call->null_completion ? NULL : &completion, diagnostic
  );
}

// A coordinate follows the map's rules: a latitude within [-90, 90] and a
// finite longitude, which wraps. Both queries need a completion.
static void malformed_queries_are_rejected_at_submission(void) {
  ridge_map view = {0};
  open_ridge_map(&view, 60.0);
  static const mln_test_validation_case pixel_cases[] = {
    {"a NaN latitude", nan_latitude, MLN_STATUS_INVALID_ARGUMENT, "latitude"},
    {"a latitude past the pole", latitude_past_the_pole,
     MLN_STATUS_INVALID_ARGUMENT, "latitude"},
    {"an infinite longitude", infinite_longitude, MLN_STATUS_INVALID_ARGUMENT,
     "longitude"},
    {"a longitude past the antimeridian", longitude_past_the_antimeridian,
     MLN_STATUS_OK, NULL},
    {"no completion", pixel_without_completion, MLN_STATUS_INVALID_ARGUMENT,
     "completion"},
  };
  const pixel_call pixel_defaults = {
    .coordinate = {.latitude = crest_latitude, .longitude = longitude}
  };
  mln_test_run_validation_table(
    pixel_cases, sizeof(pixel_cases) / sizeof(pixel_cases[0]), &pixel_defaults,
    sizeof(pixel_defaults), submit_pixel, &view.fixture.session
  );
  static const mln_test_validation_case lat_lng_cases[] = {
    {"no completion", lat_lng_without_completion, MLN_STATUS_INVALID_ARGUMENT,
     "completion"},
  };
  const lat_lng_call lat_lng_defaults = {0};
  mln_test_run_validation_table(
    lat_lng_cases, sizeof(lat_lng_cases) / sizeof(lat_lng_cases[0]),
    &lat_lng_defaults, sizeof(lat_lng_defaults), submit_lat_lng,
    &view.fixture.session
  );
  close_ridge_map(&view);
}

MLN_TEST_GROUP {
  RUN_TEST(a_finger_round_trips_through_the_ground_under_it);
  RUN_TEST(ground_behind_the_crest_is_occluded);
  RUN_TEST(the_sky_has_no_ground);
  RUN_TEST(ground_without_a_loaded_dem_tile_has_no_pixel);
  RUN_TEST(ground_lifted_past_a_float_has_no_pixel);
  RUN_TEST(without_terrain_the_answers_are_at_sea_level);
  RUN_TEST(a_session_that_never_rendered_has_no_renderer);
  RUN_TEST(malformed_queries_are_rejected_at_submission);
}
