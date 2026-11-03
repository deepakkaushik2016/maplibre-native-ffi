// How a map loads its terrain, and how its camera's center keeps to the
// ground.

#include <math.h>

#include "support/frames.h"
#include "support/map.h"
#include "support/resources.h"
#include "support/test_support.h"

// 32 by 32 Mapbox Terrain-RGB PNGs that decode to one height everywhere:
// 1000 m and 1600 m.
static const uint8_t plateau_1000_png[] = {
  0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49,
  0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x08, 0x02,
  0x00, 0x00, 0x00, 0xfc, 0x18, 0xed, 0xa3, 0x00, 0x00, 0x00, 0x2a, 0x49, 0x44,
  0x41, 0x54, 0x78, 0xda, 0xed, 0xcd, 0x41, 0x09, 0x00, 0x00, 0x08, 0x04, 0xb0,
  0xb3, 0x7f, 0x16, 0xc1, 0x88, 0xa6, 0xf0, 0x21, 0x0c, 0xf6, 0x5f, 0xaa, 0xe7,
  0x54, 0x04, 0x02, 0x81, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0xc1, 0x97, 0x60,
  0x01, 0xc1, 0x5a, 0x78, 0x4c, 0x94, 0x47, 0x38, 0x6f, 0x00, 0x00, 0x00, 0x00,
  0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};
static const uint8_t plateau_1600_png[] = {
  0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49,
  0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x08, 0x02,
  0x00, 0x00, 0x00, 0xfc, 0x18, 0xed, 0xa3, 0x00, 0x00, 0x00, 0x2a, 0x49, 0x44,
  0x41, 0x54, 0x78, 0xda, 0x63, 0x60, 0x3c, 0xaa, 0x40, 0x53, 0xc4, 0x30, 0x6a,
  0xc1, 0xa8, 0x05, 0xa3, 0x16, 0x8c, 0x5a, 0x30, 0x6a, 0xc1, 0xa8, 0x05, 0xa3,
  0x16, 0x8c, 0x5a, 0x30, 0x6a, 0xc1, 0xa8, 0x05, 0xa3, 0x16, 0x0c, 0x15, 0x0b,
  0x00, 0x56, 0xf6, 0x98, 0x2e, 0xbb, 0x29, 0xd0, 0x4f, 0x00, 0x00, 0x00, 0x00,
  0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

static const double pi = 3.14159265358979323846;

// Answers each DEM tile with its plateau and each parcels tile with no
// content, and counts the parcels tiles.
typedef struct terrain_provider {
  const uint8_t* dem;
  size_t dem_size;
  atomic_int parcel_requests;
} terrain_provider;

static bool url_contains(
  const mln_resource_request_info* request, const char* fragment
) {
  const size_t length = strlen(fragment);
  const mln_buffer_view url = request->requested_url;
  for (size_t start = 0; start + length <= url.size; start += 1) {
    if (memcmp(url.data + start, fragment, length) == 0) {
      return true;
    }
  }
  return false;
}

static void serve_terrain(
  void* user_data, const mln_resource_request_info* request,
  mln_resource_request handle
) {
  terrain_provider* provider = user_data;
  mln_resource_response response = {
    .size = sizeof(mln_resource_response),
    .status = MLN_RESOURCE_RESPONSE_STATUS_NO_CONTENT,
  };
  if (url_contains(request, "/dem/")) {
    response.status = MLN_RESOURCE_RESPONSE_STATUS_OK;
    response.bytes = provider->dem;
    response.byte_count = provider->dem_size;
  } else if (url_contains(request, "/parcels/")) {
    atomic_fetch_add(&provider->parcel_requests, 1);
  }
  mln_test_request_answer(handle, &response);
}

static mln_runtime create_terrain_runtime(terrain_provider* provider) {
  const mln_resource_provider callback = {
    .size = sizeof(mln_resource_provider),
    .callback = serve_terrain,
    .user_data = provider,
  };
  mln_runtime runtime = mln_test_create_runtime();
  MLN_TEST_OK(mln_test_set_resource_provider(runtime, &callback));
  return runtime;
}

static mln_map create_static_map(mln_runtime runtime) {
  mln_map_options options = mln_test_map_options();
  options.map_mode = MLN_MAP_MODE_STATIC;
  return mln_test_create_map_with_options(runtime, &options);
}

// Attaches an offscreen session, and sizes it, and the map with it, to
// `width` by `height`.
static void attach_sized(
  mln_map map, mln_test_render_fixture* fixture, uint32_t width, uint32_t height
) {
  TEST_ASSERT_TRUE(mln_test_render_fixture_create_offscreen(map, fixture));
  const mln_logical_size extent = {.width = width, .height = height};
  MLN_TEST_RENDER_AWAIT(
    MLN_STATUS_OK, fixture,
    mln_render_session_resize(
      fixture->session, extent, &completion.descriptor, NULL
    )
  );
}

static void set_terrain_options(
  mln_map map, const mln_map_terrain_options* options
) {
  MLN_TEST_AWAIT_OK(
    mln_map_set_terrain_options(map, options, &completion.descriptor, NULL)
  );
}

static void anchor_center_on_terrain(mln_map map) {
  MLN_TEST_AWAIT_OK(
    mln_map_anchor_center_on_terrain(map, &completion.descriptor, NULL)
  );
}

static mln_camera read_camera(mln_map map) {
  mln_camera camera = {0};
  MLN_TEST_OK(mln_test_map_get_camera(map, &camera));
  return camera;
}

// The camera's eye, in meters east, south, and up of the world's northwest
// corner, from the free camera that the snapshot reads.
typedef struct eye_meters {
  double x;
  double y;
  double z;
} eye_meters;

static eye_meters read_eye(mln_map map, double latitude) {
  MLN_TEST_OK(mln_test_map_fence(map));
  mln_map_snapshot snapshot = {.size = sizeof(mln_map_snapshot)};
  MLN_TEST_OK(mln_map_get_snapshot(map, &snapshot, NULL));
  // One Web Mercator unit spans the circumference at the latitude.
  const double meters_per_unit = 40075016.685578488 * cos(latitude * pi / 180);
  return (eye_meters){
    .x = snapshot.free_camera.position.x * meters_per_unit,
    .y = snapshot.free_camera.position.y * meters_per_unit,
    .z = snapshot.free_camera.position.z * meters_per_unit,
  };
}

// --- The load mode -------------------------------------------------------

// A plateau, and a vector source whose minzoom is deeper than the far field of
// a steep view, so the view covers it out to the horizon with more tiles than
// any load mode lets it ask for. Its layer starts hidden, so the terrain
// settles before the source asks for anything.
static const char deep_source_style[] =
  "{\"version\":8,\"center\":[-120.68,38.26],\"zoom\":13,\"pitch\":70,"
  "\"sources\":{\"dem\":{\"type\":\"raster-dem\",\"tiles\":"
  "[\"custom://dem/{z}/{x}/{y}.png\"],\"encoding\":\"mapbox\","
  "\"tileSize\":64,\"maxzoom\":6},\"parcels\":{\"type\":\"vector\","
  "\"tiles\":[\"custom://parcels/{z}/{x}/{y}.pbf\"],\"minzoom\":13,"
  "\"maxzoom\":14}},\"terrain\":{\"source\":\"dem\"},\"layers\":[{\"id\":"
  "\"parcels\",\"type\":\"fill\",\"source\":\"parcels\",\"source-layer\":"
  "\"parcels\",\"layout\":{\"visibility\":\"none\"}}]}";

// Shows the parcels over the settled terrain of a map in `mode`, and returns
// how many tiles the parcels source asked for.
static int parcel_tiles_requested(mln_terrain_load_mode mode) {
  terrain_provider provider = {
    .dem = plateau_1000_png, .dem_size = sizeof(plateau_1000_png)
  };
  mln_runtime runtime = create_terrain_runtime(&provider);
  mln_map map = create_static_map(runtime);
  const mln_map_terrain_options options = {
    .size = sizeof(mln_map_terrain_options),
    .fields = MLN_MAP_TERRAIN_OPTION_LOAD_MODE,
    .load_mode = mode,
  };
  set_terrain_options(map, &options);
  const mln_bound_options bounds = {
    .size = sizeof(mln_bound_options),
    .fields = MLN_BOUND_OPTION_MAX_PITCH,
    .max_pitch = 85.0,
  };
  MLN_TEST_AWAIT_OK(
    mln_map_set_bounds(map, &bounds, &completion.descriptor, NULL)
  );
  mln_test_load_style_and_wait(
    map, mln_buffer_view_from_cstr(deep_source_style)
  );
  mln_test_render_fixture fixture = {0};
  attach_sized(map, &fixture, 1600, 1600);
  MLN_TEST_OK(mln_test_render_still_image(&fixture, map));

  MLN_TEST_AWAIT_OK(mln_map_set_style_layer_property(
    map, MLN_BUFFER_LITERAL("parcels"), MLN_BUFFER_LITERAL("visibility"),
    MLN_BUFFER_LITERAL("\"visible\""), &completion.descriptor, NULL
  ));
  MLN_TEST_OK(mln_test_render_still_image(&fixture, map));
  const int requested = atomic_load(&provider.parcel_requests);

  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
  return requested;
}

// The load mode caps the terrain mesh at 64, 48, or 24 tiles, and a source
// past the far field's zoom asks for four tiles per mesh tile.
static void the_load_mode_caps_the_tiles_a_deep_source_asks_for(void) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(
    64 * 4, parcel_tiles_requested(MLN_TERRAIN_LOAD_MODE_QUALITY), "quality"
  );
  TEST_ASSERT_EQUAL_INT_MESSAGE(
    48 * 4, parcel_tiles_requested(MLN_TERRAIN_LOAD_MODE_BALANCED), "balanced"
  );
  TEST_ASSERT_EQUAL_INT_MESSAGE(
    24 * 4, parcel_tiles_requested(MLN_TERRAIN_LOAD_MODE_PERFORMANCE),
    "performance"
  );
}

// --- The center on the ground --------------------------------------------

// A plateau under a green fill, which the terrain drapes, over a white
// background, seen at pitch 60.
static const char plateau_style_format[] =
  "{\"version\":8,\"center\":[11.39,47.27],\"zoom\":%.2f,\"pitch\":60,"
  "\"sources\":{\"dem\":{\"type\":\"raster-dem\",\"tiles\":"
  "[\"custom://dem/{z}/{x}/{y}.png\"],\"encoding\":\"mapbox\","
  "\"tileSize\":256,\"maxzoom\":12},\"ground\":{\"type\":\"geojson\","
  "\"data\":{\"type\":\"Polygon\",\"coordinates\":[[[10,46],[13,46],"
  "[13,48.5],[10,48.5],[10,46]]]}}},\"terrain\":{\"source\":\"dem\","
  "\"exaggeration\":%.2f},\"layers\":[{\"id\":\"bg\",\"type\":"
  "\"background\",\"paint\":{\"background-color\":\"#ffffff\"}},{\"id\":"
  "\"ground\",\"type\":\"fill\",\"source\":\"ground\",\"paint\":"
  "{\"fill-color\":\"#00ff00\"}}]}";

typedef struct plateau_view {
  terrain_provider provider;
  mln_runtime runtime;
  mln_map map;
  mln_test_render_fixture fixture;
  uint32_t width;
  uint32_t height;
} plateau_view;

static void open_plateau_view(
  plateau_view* view, double zoom, double exaggeration
) {
  view->runtime = create_terrain_runtime(&view->provider);
  view->map = create_static_map(view->runtime);
  char style[sizeof(plateau_style_format) + 32];
  const int written =
    snprintf(style, sizeof(style), plateau_style_format, zoom, exaggeration);
  TEST_ASSERT_TRUE(written > 0 && (size_t)written < sizeof(style));
  mln_test_load_style_and_wait(view->map, mln_buffer_view_from_cstr(style));
  attach_sized(view->map, &view->fixture, view->width, view->height);
}

static void close_plateau_view(plateau_view* view) {
  mln_test_render_fixture_destroy(&view->fixture);
  mln_test_destroy_map(view->map);
  mln_test_destroy_runtime(view->runtime);
}

// Renders a still image and counts the pixels of the green ground. Red and
// blue are both low on green, so the count holds for RGBA and BGRA alike.
static size_t render_ground_pixels(plateau_view* view) {
  enum { capacity = 400 * 900 * 4 };
  static uint8_t pixels[capacity];
  const size_t byte_length = (size_t)view->width * view->height * 4;
  TEST_ASSERT_LESS_OR_EQUAL_size_t(capacity, byte_length);
  MLN_TEST_OK(mln_test_render_still_image(&view->fixture, view->map));
  mln_texture_image_info info = {0};
  MLN_TEST_OK(
    mln_test_render_read_back(&view->fixture, &info, pixels, capacity)
  );
  TEST_ASSERT_EQUAL_size_t(byte_length, info.byte_length);
  size_t ground = 0;
  for (size_t pixel = 0; pixel < byte_length; pixel += 4) {
    if (
      pixels[pixel + 1] > 160 && pixels[pixel] < 96 && pixels[pixel + 2] < 96
    ) {
      ground += 1;
    }
  }
  return ground;
}

static void anchoring_without_terrain_leaves_the_camera(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_camera_update update = {
    .size = sizeof(mln_camera_update),
    .camera = {
      .size = sizeof(mln_camera_options),
      .fields = MLN_CAMERA_OPTION_CENTER | MLN_CAMERA_OPTION_ZOOM |
                MLN_CAMERA_OPTION_PITCH,
      .center = {.latitude = 47.27, .longitude = 11.39},
      .zoom = 12.0,
      .pitch = 60.0,
    },
  };
  MLN_TEST_AWAIT_OK(
    mln_map_update_camera(map, &update, &completion.descriptor, NULL)
  );
  const mln_camera before = read_camera(map);

  anchor_center_on_terrain(map);

  const mln_camera after = read_camera(map);
  TEST_ASSERT_EQUAL_DOUBLE(before.center.latitude, after.center.latitude);
  TEST_ASSERT_EQUAL_DOUBLE(before.center.longitude, after.center.longitude);
  TEST_ASSERT_EQUAL_DOUBLE(before.zoom, after.zoom);
  TEST_ASSERT_EQUAL_DOUBLE(before.center_altitude, after.center_altitude);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

// Anchoring slides the center up the line of sight onto a 1000 m plateau: the
// eye stays where it was, and the zoom rises by the distance saved.
static void anchoring_keeps_the_eye_and_raises_the_zoom(void) {
  plateau_view view = {
    .provider = {.dem = plateau_1000_png, .dem_size = sizeof(plateau_1000_png)},
    .width = 256,
    .height = 256,
  };
  open_plateau_view(&view, 12.0, 1.0);
  // The frame reports the ground under the center.
  TEST_ASSERT_GREATER_THAN_size_t(0, render_ground_pixels(&view));
  const mln_camera before = read_camera(view.map);
  const eye_meters eye_before = read_eye(view.map, before.center.latitude);
  TEST_ASSERT_EQUAL_DOUBLE(0.0, before.center_altitude);

  anchor_center_on_terrain(view.map);

  const mln_camera after = read_camera(view.map);
  const eye_meters eye_after = read_eye(view.map, before.center.latitude);
  TEST_ASSERT_DOUBLE_WITHIN(1.0, 1000.0, after.center_altitude);
  TEST_ASSERT_DOUBLE_WITHIN(1.0, eye_before.x, eye_after.x);
  TEST_ASSERT_DOUBLE_WITHIN(1.0, eye_before.y, eye_after.y);
  TEST_ASSERT_DOUBLE_WITHIN(1.0, eye_before.z, eye_after.z);
  TEST_ASSERT_GREATER_THAN_DOUBLE(before.zoom + 0.1, after.zoom);
  close_plateau_view(&view);
}

// The app's trail view on a 390 by 844 phone: z14.5 at pitch 60, over a
// 1600 m plateau that the terrain draws 1.25 times as high. A center at sea
// level puts the eye about 1450 m up, inside the 2000 m ground, and the frame
// shows none of it.
enum { blank_ground_pixels = 16 };

static void open_trail_view(plateau_view* view) {
  *view = (plateau_view){
    .provider = {.dem = plateau_1600_png, .dem_size = sizeof(plateau_1600_png)},
    .width = 390,
    .height = 844,
  };
  open_plateau_view(view, 14.5, 1.25);
  TEST_ASSERT_LESS_THAN_size_t_MESSAGE(
    blank_ground_pixels, render_ground_pixels(view),
    "the camera under the ground saw it"
  );
  const mln_camera camera = read_camera(view->map);
  TEST_ASSERT_LESS_THAN_DOUBLE(
    2000.0, read_eye(view->map, camera.center.latitude).z
  );
}

static void the_clamp_lifts_a_camera_out_of_the_ground(void) {
  plateau_view view;
  open_trail_view(&view);

  const mln_map_terrain_options options = {
    .size = sizeof(mln_map_terrain_options),
    .fields = MLN_MAP_TERRAIN_OPTION_CENTER_CLAMPED_TO_GROUND,
    .center_clamped_to_ground = true,
  };
  set_terrain_options(view.map, &options);

  TEST_ASSERT_DOUBLE_WITHIN(1.0, 2000.0, read_camera(view.map).center_altitude);
  TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(
    (size_t)view.width * view.height / 2, render_ground_pixels(&view),
    "the clamped camera saw too little ground"
  );
  close_plateau_view(&view);
}

// Anchoring keeps the camera where it is, so a camera inside the ground stays
// there and sees nothing. Only the clamp lifts it out.
static void anchoring_leaves_a_camera_in_the_ground(void) {
  plateau_view view;
  open_trail_view(&view);
  const mln_camera before = read_camera(view.map);

  anchor_center_on_terrain(view.map);

  const mln_camera after = read_camera(view.map);
  TEST_ASSERT_EQUAL_DOUBLE(before.center_altitude, after.center_altitude);
  TEST_ASSERT_EQUAL_DOUBLE(before.zoom, after.zoom);
  TEST_ASSERT_LESS_THAN_size_t(
    blank_ground_pixels, render_ground_pixels(&view)
  );
  close_plateau_view(&view);
}

MLN_TEST_GROUP {
  RUN_TEST(the_load_mode_caps_the_tiles_a_deep_source_asks_for);
  RUN_TEST(anchoring_without_terrain_leaves_the_camera);
  RUN_TEST(anchoring_keeps_the_eye_and_raises_the_zoom);
  RUN_TEST(the_clamp_lifts_a_camera_out_of_the_ground);
  RUN_TEST(anchoring_leaves_a_camera_in_the_ground);
}
