// The terrain elevation that a render session reads from its last frame.

#include <math.h>

#include "support/map.h"
#include "support/resources.h"
#include "support/terrain.h"
#include "support/test_support.h"

// A static map over the 1000 m plateau, whose terrain draws it `exaggeration`
// times as high, seen from above.
static const char plateau_style_format[] =
  "{\"version\":8,\"center\":[11.39,47.27],\"zoom\":10,"
  "\"sources\":{\"dem\":{\"type\":\"raster-dem\",\"tiles\":"
  "[\"custom://dem/{z}/{x}/{y}.png\"],\"encoding\":\"mapbox\","
  "\"tileSize\":256,\"maxzoom\":12}},\"terrain\":{\"source\":\"dem\","
  "\"exaggeration\":%.2f},\"layers\":[{\"id\":\"bg\",\"type\":"
  "\"background\",\"paint\":{\"background-color\":\"#ffffff\"}}]}";

static const mln_lat_lng on_the_plateau = {
  .latitude = 47.27, .longitude = 11.39
};
// Half a world away, where the view loads no DEM tile.
static const mln_lat_lng far_away = {.latitude = -33.86, .longitude = -168.79};

typedef struct plateau_map {
  mln_test_dem_tile tile;
  mln_runtime runtime;
  mln_map map;
  mln_test_render_fixture fixture;
} plateau_map;

static void open_plateau_map(plateau_map* view, double exaggeration) {
  view->tile = (mln_test_dem_tile){
    .bytes = mln_test_plateau_1000_png, .size = mln_test_plateau_1000_png_size
  };
  const mln_resource_provider provider = mln_test_dem_provider(&view->tile);
  view->runtime = mln_test_create_runtime();
  MLN_TEST_OK(mln_test_set_resource_provider(view->runtime, &provider));
  mln_map_options options = mln_test_map_options();
  options.initial_extent =
    (mln_logical_extent){.width = 64, .height = 64, .scale_factor = 1.0};
  options.map_mode = MLN_MAP_MODE_STATIC;
  view->map = mln_test_create_map_with_options(view->runtime, &options);
  char style[sizeof(plateau_style_format) + 32];
  const int written =
    snprintf(style, sizeof(style), plateau_style_format, exaggeration);
  TEST_ASSERT_TRUE(written > 0 && (size_t)written < sizeof(style));
  mln_test_load_style_and_wait(view->map, mln_buffer_view_from_cstr(style));
  TEST_ASSERT_TRUE(
    mln_test_render_fixture_create_offscreen(view->map, &view->fixture)
  );
}

static void close_plateau_map(plateau_map* view) {
  mln_test_render_fixture_destroy(&view->fixture);
  mln_test_destroy_map(view->map);
  mln_test_destroy_runtime(view->runtime);
}

// Queries the elevation at `point` and returns the completion's status.
// `*out_found` reports whether the query delivered a height, and only then is
// it copied to `*out_meters`.
static mln_status query_elevation(
  const mln_test_render_fixture* fixture, mln_lat_lng point, double* out_meters,
  bool* out_found
) {
  *out_found = false;
  mln_test_completion completion =
    mln_test_completion_default(sizeof(mln_terrain_elevation));
  const mln_status submitted = mln_render_session_query_terrain_elevation(
    fixture->session, point, &completion.descriptor, MLN_TEST_DIAGNOSTIC
  );
  if (submitted != MLN_STATUS_OK) {
    completion.descriptor.release_user_data(completion.descriptor.user_data);
    mln_test_completion_destroy(&completion);
    return submitted;
  }
  mln_status status =
    mln_test_render_fixture_finish_operation(fixture, &completion);
  *out_found = status == MLN_STATUS_OK &&
               mln_test_completion_value_count(&completion) != 0;
  mln_terrain_elevation elevation = {0};
  if (
    *out_found && !mln_test_completion_copy_value(
                    &completion, &elevation, sizeof(mln_terrain_elevation)
                  )
  ) {
    status = MLN_STATUS_NATIVE_ERROR;
  }
  if (*out_found) {
    *out_meters = elevation.meters;
  }
  mln_test_completion_destroy(&completion);
  return status;
}

#define EXPECT_ELEVATION(expected, fixture, point)                     \
  do {                                                                 \
    double meters = NAN;                                               \
    bool found = false;                                                \
    MLN_TEST_OK(query_elevation((fixture), (point), &meters, &found)); \
    TEST_ASSERT_TRUE_MESSAGE(found, "the query found no terrain");     \
    TEST_ASSERT_DOUBLE_WITHIN(0.5, (expected), meters);                \
  } while (false)

#define EXPECT_NO_ELEVATION(fixture, point)                            \
  do {                                                                 \
    double meters = NAN;                                               \
    bool found = true;                                                 \
    MLN_TEST_OK(query_elevation((fixture), (point), &meters, &found)); \
    TEST_ASSERT_FALSE_MESSAGE(found, "the query found terrain");       \
  } while (false)

static void set_terrain(mln_map map, const char* json) {
  MLN_TEST_AWAIT_OK(mln_map_set_style_terrain_json(
    map, mln_buffer_view_from_cstr(json), &completion.descriptor, NULL
  ));
}

// The height comes from the DEM tile under the point, as the terrain draws
// it: exaggeration scales it, and a point that no loaded tile covers has none.
static void the_height_is_the_terrain_the_last_frame_drew(void) {
  plateau_map view = {0};
  open_plateau_map(&view, 1.0);
  MLN_TEST_OK(mln_test_render_still_image(&view.fixture, view.map));
  EXPECT_ELEVATION(1000.0, &view.fixture, on_the_plateau);
  EXPECT_NO_ELEVATION(&view.fixture, far_away);

  set_terrain(view.map, "{\"source\":\"dem\",\"exaggeration\":2}");
  MLN_TEST_OK(mln_test_render_still_image(&view.fixture, view.map));
  EXPECT_ELEVATION(2000.0, &view.fixture, on_the_plateau);

  // The DEM tiles stay loaded, but a map without terrain has no height.
  set_terrain(view.map, "null");
  MLN_TEST_OK(mln_test_render_still_image(&view.fixture, view.map));
  EXPECT_NO_ELEVATION(&view.fixture, on_the_plateau);
  close_plateau_map(&view);
}

// The largest exaggeration a float holds lifts the plateau past what a float
// can hold. A height that isn't a finite number of meters is no value.
static void a_height_past_a_float_is_no_value(void) {
  plateau_map view = {0};
  open_plateau_map(&view, 1.0);
  set_terrain(
    view.map, "{\"source\":\"dem\",\"exaggeration\":3.4028234663852886e38}"
  );
  MLN_TEST_OK(mln_test_render_still_image(&view.fixture, view.map));
  EXPECT_NO_ELEVATION(&view.fixture, on_the_plateau);
  close_plateau_map(&view);
}

// Terrain may name a source that holds no DEM, such as a GeoJSON source that a
// layer draws. The map accepts it and renders, but the terrain has no height.
static void terrain_on_a_source_without_dem_has_no_height(void) {
  plateau_map view = {0};
  open_plateau_map(&view, 1.0);
  MLN_TEST_AWAIT_OK(mln_map_add_style_source_json(
    view.map, MLN_BUFFER_LITERAL("trail"),
    MLN_BUFFER_LITERAL(
      "{\"type\":\"geojson\",\"data\":{\"type\":\"LineString\","
      "\"coordinates\":[[11.3,47.27],[11.5,47.27]]}}"
    ),
    &completion.descriptor, NULL
  ));
  MLN_TEST_AWAIT_OK(mln_map_add_style_layer_json(
    view.map,
    MLN_BUFFER_LITERAL(
      "{\"id\":\"trail\",\"type\":\"line\",\"source\":\"trail\"}"
    ),
    (mln_buffer_view){0}, &completion.descriptor, NULL
  ));
  set_terrain(view.map, "{\"source\":\"trail\"}");
  MLN_TEST_OK(mln_test_render_still_image(&view.fixture, view.map));
  EXPECT_NO_ELEVATION(&view.fixture, on_the_plateau);
  close_plateau_map(&view);
}

// A session creates its renderer for its first frame, so before that the
// query fails as every other render session query does.
static void a_session_that_never_rendered_has_no_renderer(void) {
  plateau_map view = {0};
  open_plateau_map(&view, 1.0);
  double meters = NAN;
  bool found = false;
  MLN_TEST_INVALID_STATE(
    query_elevation(&view.fixture, on_the_plateau, &meters, &found)
  );
  close_plateau_map(&view);
}

typedef struct elevation_call {
  mln_lat_lng point;
  bool null_completion;
} elevation_call;

static void nan_latitude(void* call) {
  ((elevation_call*)call)->point.latitude = (double)NAN;
}
static void latitude_past_the_pole(void* call) {
  ((elevation_call*)call)->point.latitude = 90.5;
}
static void latitude_past_the_south_pole(void* call) {
  ((elevation_call*)call)->point.latitude = -90.5;
}
static void infinite_longitude(void* call) {
  ((elevation_call*)call)->point.longitude = (double)INFINITY;
}
static void longitude_past_the_antimeridian(void* call) {
  ((elevation_call*)call)->point.longitude = 191.39;
}
static void null_completion(void* call) {
  ((elevation_call*)call)->null_completion = true;
}

static mln_status submit_elevation(
  void* context, const void* descriptor, mln_diagnostic* diagnostic
) {
  const mln_render_session session = *(const mln_render_session*)context;
  const elevation_call* call = descriptor;
  mln_completion completion = mln_test_discard_completion();
  return mln_render_session_query_terrain_elevation(
    session, call->point, call->null_completion ? NULL : &completion, diagnostic
  );
}

// Coordinates follow the map's rules: a latitude within [-90, 90] and a
// finite longitude, which wraps.
static void malformed_queries_are_rejected_at_submission(void) {
  plateau_map view = {0};
  open_plateau_map(&view, 1.0);
  static const mln_test_validation_case cases[] = {
    {"a NaN latitude", nan_latitude, MLN_STATUS_INVALID_ARGUMENT, "latitude"},
    {"a latitude past the north pole", latitude_past_the_pole,
     MLN_STATUS_INVALID_ARGUMENT, "latitude"},
    {"a latitude past the south pole", latitude_past_the_south_pole,
     MLN_STATUS_INVALID_ARGUMENT, "latitude"},
    {"an infinite longitude", infinite_longitude, MLN_STATUS_INVALID_ARGUMENT,
     "longitude"},
    {"a longitude past the antimeridian", longitude_past_the_antimeridian,
     MLN_STATUS_OK, NULL},
    {"no completion", null_completion, MLN_STATUS_INVALID_ARGUMENT,
     "completion"},
  };
  const elevation_call defaults = {.point = on_the_plateau};
  mln_test_run_validation_table(
    cases, sizeof(cases) / sizeof(cases[0]), &defaults, sizeof(defaults),
    submit_elevation, &view.fixture.session
  );
  close_plateau_map(&view);
}

MLN_TEST_GROUP {
  RUN_TEST(the_height_is_the_terrain_the_last_frame_drew);
  RUN_TEST(a_height_past_a_float_is_no_value);
  RUN_TEST(terrain_on_a_source_without_dem_has_no_height);
  RUN_TEST(a_session_that_never_rendered_has_no_renderer);
  RUN_TEST(malformed_queries_are_rejected_at_submission);
}
