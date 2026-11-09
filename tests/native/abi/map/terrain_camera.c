// How a map loads its terrain, and how its camera's center keeps to the
// ground.

#include <math.h>

#include "support/camera.h"
#include "support/frames.h"
#include "support/map.h"
#include "support/resources.h"
#include "support/terrain.h"
#include "support/test_support.h"

// 256 by 256 Mapbox Terrain-RGB PNGs for the z9 tile 272/180, with ground
// that steps up at pixel row 64 (46.92026 N): from sea level to 1455 m, and
// from 100 m to 1555 m.
static const uint8_t sea_cliff_tile_png[] = {
  0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49,
  0x48, 0x44, 0x52, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x02,
  0x00, 0x00, 0x00, 0xd3, 0x10, 0x3f, 0x31, 0x00, 0x00, 0x02, 0x07, 0x49, 0x44,
  0x41, 0x54, 0x78, 0xda, 0xed, 0xd3, 0x41, 0x0d, 0x00, 0x30, 0x08, 0x00, 0x31,
  0x66, 0x1a, 0x09, 0xd3, 0x31, 0x27, 0xb3, 0x86, 0x09, 0x5e, 0xd0, 0xa4, 0x0a,
  0x2e, 0xb9, 0x38, 0xf7, 0xc1, 0x5a, 0x21, 0x01, 0x06, 0x00, 0x03, 0x80, 0x01,
  0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03,
  0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06,
  0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c,
  0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18,
  0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30,
  0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60,
  0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0x30,
  0x80, 0x0a, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00,
  0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x73, 0x06, 0xf8, 0x09, 0x6b,
  0x19, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00,
  0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00,
  0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00,
  0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00,
  0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01,
  0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03,
  0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06,
  0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x18, 0x40, 0x05, 0x0c, 0x00, 0x06, 0x00,
  0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00,
  0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00,
  0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00,
  0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00,
  0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00,
  0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01,
  0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03,
  0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01,
  0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03,
  0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06,
  0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c,
  0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18,
  0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30,
  0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60,
  0x00, 0x30, 0x00, 0x18, 0x00, 0x03, 0x48, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00,
  0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00,
  0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01,
  0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03,
  0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06,
  0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x68, 0x55, 0x46, 0x1f, 0x51,
  0xef, 0x0d, 0xf9, 0xda, 0xd1, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
  0xae, 0x42, 0x60, 0x82
};
static const uint8_t ledge_tile_png[] = {
  0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49,
  0x48, 0x44, 0x52, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x02,
  0x00, 0x00, 0x00, 0xd3, 0x10, 0x3f, 0x31, 0x00, 0x00, 0x02, 0x07, 0x49, 0x44,
  0x41, 0x54, 0x78, 0xda, 0xed, 0xd3, 0x41, 0x0d, 0x00, 0x30, 0x08, 0x00, 0x31,
  0x66, 0x14, 0x3d, 0xcb, 0x6c, 0xcd, 0x18, 0x26, 0x78, 0x41, 0x93, 0x2a, 0xb8,
  0xe4, 0xe2, 0xbc, 0x0b, 0x6b, 0x85, 0x04, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00,
  0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00,
  0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00,
  0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00,
  0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00,
  0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00,
  0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01,
  0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0xc0, 0x00,
  0x2a, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80,
  0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0xcc, 0x19, 0xe0, 0x27, 0xac, 0x65,
  0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30,
  0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60,
  0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0,
  0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80,
  0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00,
  0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00,
  0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00,
  0x0c, 0x00, 0x06, 0x00, 0x03, 0x60, 0x00, 0x15, 0x30, 0x00, 0x18, 0x00, 0x0c,
  0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18,
  0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30,
  0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60,
  0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0,
  0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80,
  0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00,
  0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x80,
  0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00,
  0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00,
  0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00,
  0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00,
  0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00,
  0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00,
  0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01,
  0xc0, 0x00, 0x60, 0x00, 0x0c, 0x20, 0x01, 0x06, 0x00, 0x03, 0x80, 0x01, 0xc0,
  0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x03, 0x80,
  0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x06, 0x00,
  0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00, 0x0c, 0x00,
  0x06, 0x00, 0x03, 0x80, 0x01, 0xc0, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00,
  0x0c, 0x00, 0x06, 0x00, 0x03, 0x80, 0x01, 0xa0, 0x55, 0x01, 0x97, 0x1a, 0x50,
  0xc3, 0x4b, 0x49, 0x31, 0x18, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
  0xae, 0x42, 0x60, 0x82
};
static const double tile_step_latitude = 46.92025531537452;

// A 32 by 32 Mapbox Terrain-RGB PNG like mln_test_plateau_1000_png, at 100 m.
static const uint8_t lowland_png[] = {
  0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49,
  0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x08, 0x02,
  0x00, 0x00, 0x00, 0xfc, 0x18, 0xed, 0xa3, 0x00, 0x00, 0x00, 0x28, 0x49, 0x44,
  0x41, 0x54, 0x78, 0xda, 0xed, 0xcd, 0xb1, 0x09, 0x00, 0x00, 0x08, 0x03, 0xb0,
  0xfa, 0xbd, 0x78, 0xb5, 0x57, 0x38, 0x08, 0x81, 0xec, 0x49, 0x4d, 0x9f, 0x8a,
  0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x81, 0x40, 0x20, 0xf8, 0x12, 0x2c, 0x36,
  0xd4, 0x4c, 0x3d, 0xc4, 0x27, 0x79, 0xc8, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
  0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
};

static const double pi = 3.14159265358979323846;

// Answers each DEM tile with its plateau and each parcels tile with no
// content, and counts the parcels tiles. With `south` set, the DEM tiles
// south of the z9 tile edge at `step_latitude` hold `south` instead, so the
// ground steps up there.
typedef struct terrain_provider {
  const uint8_t* dem;
  size_t dem_size;
  const uint8_t* south;
  size_t south_size;
  // With `step_tile` set, the z9 DEM tile 272/180 holds it instead.
  const uint8_t* step_tile;
  size_t step_tile_size;
  atomic_int parcel_requests;
} terrain_provider;

// The northern edge of the z9 tiles in row 180.
static const double step_latitude = 47.04018214480666;
enum { step_row = 180, step_zoom = 9 };

// Whether the DEM tile that `request` asks for lies south of the step.
static bool south_of_step(const mln_resource_request_info* request) {
  char url[128];
  if (request->requested_url.size >= sizeof(url)) {
    return false;
  }
  memcpy(url, request->requested_url.data, request->requested_url.size);
  url[request->requested_url.size] = '\0';
  const char* tile = strstr(url, "/dem/");
  unsigned z = 0;
  unsigned x = 0;
  unsigned y = 0;
  if (tile == NULL || sscanf(tile, "/dem/%u/%u/%u", &z, &x, &y) != 3) {
    return false;
  }
  return z >= step_zoom && y >= ((unsigned)step_row << (z - step_zoom));
}

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
    const bool south = provider->south != NULL && south_of_step(request);
    response.status = MLN_RESOURCE_RESPONSE_STATUS_OK;
    response.bytes = south ? provider->south : provider->dem;
    response.byte_count = south ? provider->south_size : provider->dem_size;
    if (
      provider->step_tile != NULL && url_contains(request, "/dem/9/272/180.")
    ) {
      response.bytes = provider->step_tile;
      response.byte_count = provider->step_tile_size;
    }
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
// a steep view. With the level of detail off, the view covers that source at
// its minzoom out to the horizon, with more tiles than any load mode lets it
// ask for. Its layer starts hidden, so the terrain settles before the source
// asks for anything.
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
    .dem = mln_test_plateau_1000_png, .dem_size = mln_test_plateau_1000_png_size
  };
  mln_runtime runtime = create_terrain_runtime(&provider);
  mln_map map = create_static_map(runtime);
  // A pitched view with the level of detail on leaves out the far tiles that
  // it wants coarser than the source's minzoom, so it covers too few to cap.
  const mln_map_tile_options flat_detail = {
    .size = sizeof(mln_map_tile_options),
    .fields = MLN_MAP_TILE_OPTION_LOD_PITCH_THRESHOLD,
    .lod_pitch_threshold = 180.0,
  };
  MLN_TEST_AWAIT_OK(
    mln_map_set_tile_options(map, &flat_detail, &completion.descriptor, NULL)
  );
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
    .provider =
      {.dem = mln_test_plateau_1000_png,
       .dem_size = mln_test_plateau_1000_png_size},
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
    .provider =
      {.dem = mln_test_plateau_1600_png,
       .dem_size = mln_test_plateau_1600_png_size},
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

// A clamped still image: a 1000 m plateau above the eye of a camera at 0 m. The
// first frame tells the map the height, and the map draws again from the
// ground, on the update the clamp published. One still image only, so a still
// image that ends on the first frame, or waits for a frame nothing demands, is
// a failure rather than a blank picture or a hang.
static void a_clamped_still_image_draws_the_ground_it_lands_on(void) {
  plateau_view view = {
    .provider =
      {.dem = mln_test_plateau_1000_png,
       .dem_size = mln_test_plateau_1000_png_size},
    .width = 256,
    .height = 256,
  };
  open_plateau_view(&view, 14.5, 1.0);
  const mln_map_terrain_options options = {
    .size = sizeof(mln_map_terrain_options),
    .fields = MLN_MAP_TERRAIN_OPTION_CENTER_CLAMPED_TO_GROUND,
    .center_clamped_to_ground = true,
  };
  set_terrain_options(view.map, &options);

  TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(
    (size_t)view.width * view.height / 2, render_ground_pixels(&view),
    "the still image drew from under the ground"
  );
  TEST_ASSERT_DOUBLE_WITHIN(1.0, 1000.0, read_camera(view.map).center_altitude);
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

// --- The ground under the eye -------------------------------------------

// Lowland north of the step and higher ground south of it, seen at pitch 60
// with the terrain at its true height.
static const char step_style_format[] =
  "{\"version\":8,\"center\":[11.39,%.6f],\"zoom\":%.2f,\"pitch\":60,"
  "\"bearing\":%.0f,\"sources\":{\"dem\":{\"type\":\"raster-dem\","
  "\"tiles\":[\"custom://dem/{z}/{x}/{y}.png\"],\"encoding\":\"mapbox\","
  "\"tileSize\":256,\"maxzoom\":%d}},\"terrain\":{\"source\":\"dem\","
  "\"exaggeration\":1},\"layers\":[{\"id\":\"bg\",\"type\":"
  "\"background\",\"paint\":{\"background-color\":\"#ffffff\"}}]}";

static mln_map open_step_map(
  mln_runtime runtime, mln_map_mode mode, double latitude, double zoom,
  double bearing, int dem_max_zoom, mln_test_render_fixture* fixture
) {
  mln_map_options options = mln_test_map_options();
  options.map_mode = mode;
  mln_map map = mln_test_create_map_with_options(runtime, &options);
  const mln_map_terrain_options clamp = {
    .size = sizeof(mln_map_terrain_options),
    .fields = MLN_MAP_TERRAIN_OPTION_CENTER_CLAMPED_TO_GROUND,
    .center_clamped_to_ground = true,
  };
  set_terrain_options(map, &clamp);
  char style[sizeof(step_style_format) + 64];
  const int written = snprintf(
    style, sizeof(style), step_style_format, latitude, zoom, bearing,
    dem_max_zoom
  );
  TEST_ASSERT_TRUE(written > 0 && (size_t)written < sizeof(style));
  mln_test_load_style_and_wait(map, mln_buffer_view_from_cstr(style));
  attach_sized(map, fixture, 390, 844);
  return map;
}

// The phone's trail view looks north from a ledge: the center is 1 km north
// of the step, over the lowland, and the eye about 2.5 km south of it and
// 1460 m above it. On the lowland the eye would be 5 m above the ledge. The
// clamp keeps the eye 10 m above the ground under it, which lifts the center
// 5 m off the lowland. One DEM tile holds the lowland, the step, and the
// ledge, so the frame that knows the ground under the center also knows the
// ground under the eye.
static void expect_the_eye_lifted_above_the_ledge(
  const uint8_t* tile, size_t tile_size, double lowland_meters
) {
  terrain_provider provider = {
    .dem = mln_test_plateau_1000_png,
    .dem_size = mln_test_plateau_1000_png_size,
    .step_tile = tile,
    .step_tile_size = tile_size,
  };
  mln_runtime runtime = create_terrain_runtime(&provider);
  mln_test_render_fixture fixture = {0};
  mln_map map = open_step_map(
    runtime, MLN_MAP_MODE_STATIC, tile_step_latitude + 0.009, 14.5, 0.0, 9,
    &fixture
  );

  // The heights reach the map with the frame, and the clamp moves the
  // camera after it.
  MLN_TEST_OK(mln_test_render_still_image(&fixture, map));
  MLN_TEST_OK(mln_test_render_still_image(&fixture, map));

  const mln_camera camera = read_camera(map);
  const eye_meters eye = read_eye(map, camera.center.latitude);
  TEST_ASSERT_DOUBLE_WITHIN(1.0, lowland_meters + 1465.0, eye.z);
  TEST_ASSERT_GREATER_THAN_DOUBLE(lowland_meters + 1.0, camera.center_altitude);
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

// A 1555 m ledge over lowland at 100 m.
static void the_clamp_keeps_the_eye_above_the_ground_under_it(void) {
  expect_the_eye_lifted_above_the_ledge(
    ledge_tile_png, sizeof(ledge_tile_png), 100.0
  );
}

// A 1455 m sea cliff over ground at exactly sea level, as on a coastal trail.
static void the_clamp_keeps_the_eye_above_a_sea_cliff(void) {
  expect_the_eye_lifted_above_the_ledge(
    sea_cliff_tile_png, sizeof(sea_cliff_tile_png), 0.0
  );
}

// --- An animation onto higher ground -------------------------------------

// Renders one frame of a continuous map and reads the camera it published.
static mln_map_snapshot render_frame_snapshot(
  const mln_test_render_fixture* fixture, mln_map map
) {
  mln_frame_demand demand = {.size = sizeof(mln_frame_demand)};
  MLN_TEST_OK(
    mln_render_session_demand_frame(fixture->session, &demand, NULL)
  );
  mln_render_frame_batch_release(mln_test_render_wait_for_results(fixture));
  MLN_TEST_OK(mln_test_map_fence(map));
  mln_map_snapshot snapshot = {.size = sizeof(mln_map_snapshot)};
  MLN_TEST_OK(mln_map_get_snapshot(map, &snapshot, NULL));
  return snapshot;
}

// The clamped center rides the lowland, looking south over the step at the
// plateau. A two-second ease carries the center 9 km south onto the plateau.
// The map learns the ground there while the ease runs, so the center's height
// eases up with the move rather than staying at 100 m and jumping 1500 m once
// the ease ends.
static void an_ease_onto_higher_ground_eases_the_center_up(void) {
  terrain_provider provider = {
    .dem = lowland_png,
    .dem_size = sizeof(lowland_png),
    .south = mln_test_plateau_1600_png,
    .south_size = mln_test_plateau_1600_png_size,
  };
  mln_runtime runtime = create_terrain_runtime(&provider);
  mln_test_render_fixture fixture = {0};
  mln_map map = open_step_map(
    runtime, MLN_MAP_MODE_CONTINUOUS, step_latitude + 0.05, 14.5, 180.0, 12,
    &fixture
  );
  mln_test_render_until_idle(runtime, map, &fixture);
  double previous = read_camera(map).center_altitude;
  TEST_ASSERT_DOUBLE_WITHIN(1.0, 100.0, previous);

  mln_test_transition_end probe;
  mln_test_transition_end_init(&probe);
  mln_camera_update ease = {
    .size = sizeof(mln_camera_update),
    .mode = MLN_CAMERA_UPDATE_MODE_EASE,
    .camera =
      {
        .size = sizeof(mln_camera_options),
        .fields = MLN_CAMERA_OPTION_CENTER,
        .center = {.latitude = step_latitude - 0.03, .longitude = 11.39},
      },
    .animation = {
      .fields = MLN_ANIMATION_OPTION_DURATION | MLN_ANIMATION_OPTION_EASING,
      .duration_ms = 2000.0,
      .easing = {.x1 = 0.0, .y1 = 0.0, .x2 = 1.0, .y2 = 1.0},
      .end_handler = mln_test_transition_end_handler(&probe),
    },
  };
  MLN_TEST_AWAIT_OK(
    mln_map_update_camera(map, &ease, &completion.descriptor, NULL)
  );

  // Each frame advances the ease on the map's clock. Record the height of
  // every frame until the ease ends, and of the settled map after it.
  double largest_step = 0.0;
  size_t between = 0;
  const mln_test_deadline deadline = mln_test_deadline_default();
  while (atomic_load(&probe.releases) == 0) {
    TEST_ASSERT_FALSE_MESSAGE(
      mln_test_deadline_passed(deadline), "the ease never ended"
    );
    const mln_map_snapshot frame = render_frame_snapshot(&fixture, map);
    const double altitude = frame.view.camera.center_altitude;
    TEST_ASSERT_GREATER_OR_EQUAL_DOUBLE(previous - 0.5, altitude);
    largest_step = fmax(largest_step, altitude - previous);
    if (frame.transition_in_progress && altitude > 101.0 && altitude < 1599.0) {
      between += 1;
    }
    previous = altitude;
  }
  mln_test_render_until_idle(runtime, map, &fixture);
  const double settled = read_camera(map).center_altitude;
  largest_step = fmax(largest_step, settled - previous);

  TEST_ASSERT_DOUBLE_WITHIN(1.0, 1600.0, settled);
  TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(
    0, between, "no frame of the ease was between the two heights"
  );
  TEST_ASSERT_LESS_THAN_DOUBLE_MESSAGE(
    750.0, largest_step, "the center's height jumped"
  );
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

// --- A move onto ground still loading -----------------------------------

// Serves the step's terrain, except that while `holding` it holds every DEM
// tile that covers `target`, at any zoom, so no coarser tile answers for it.
typedef struct held_terrain_provider {
  terrain_provider terrain;
  mln_lat_lng target;
  atomic_flag lock;
  bool holding;
  size_t held_count;
  mln_resource_request held[32];
  bool held_south[32];
} held_terrain_provider;

static void lock_provider(held_terrain_provider* provider) {
  while (atomic_flag_test_and_set(&provider->lock)) {
  }
}

static bool covers_target(
  const held_terrain_provider* provider,
  const mln_resource_request_info* request
) {
  char url[128];
  if (request->requested_url.size >= sizeof(url)) {
    return false;
  }
  memcpy(url, request->requested_url.data, request->requested_url.size);
  url[request->requested_url.size] = '\0';
  const char* tile = strstr(url, "/dem/");
  unsigned z = 0;
  unsigned x = 0;
  unsigned y = 0;
  if (tile == NULL || sscanf(tile, "/dem/%u/%u/%u", &z, &x, &y) != 3) {
    return false;
  }
  const double tiles = ldexp(1.0, (int)z);
  const double latitude = provider->target.latitude * pi / 180.0;
  const double target_x = (provider->target.longitude + 180.0) / 360.0 * tiles;
  const double target_y =
    (1.0 - log(tan(latitude) + 1.0 / cos(latitude)) / pi) / 2.0 * tiles;
  return x == (unsigned)floor(target_x) && y == (unsigned)floor(target_y);
}

static void serve_held_terrain(
  void* user_data, const mln_resource_request_info* request,
  mln_resource_request handle
) {
  held_terrain_provider* provider = user_data;
  if (covers_target(provider, request)) {
    lock_provider(provider);
    const bool hold =
      provider->holding &&
      provider->held_count < sizeof(provider->held) / sizeof(provider->held[0]);
    if (hold) {
      provider->held[provider->held_count] = handle;
      provider->held_south[provider->held_count] = south_of_step(request);
      provider->held_count += 1;
    }
    atomic_flag_clear(&provider->lock);
    if (hold) {
      return;
    }
  }
  serve_terrain(&provider->terrain, request, handle);
}

// Answers every held tile, and serves the rest as they come.
static void release_held_terrain(held_terrain_provider* provider) {
  lock_provider(provider);
  provider->holding = false;
  atomic_flag_clear(&provider->lock);
  for (size_t index = 0; index < provider->held_count; ++index) {
    const bool south = provider->held_south[index];
    const mln_resource_response response = {
      .size = sizeof(mln_resource_response),
      .status = MLN_RESOURCE_RESPONSE_STATUS_OK,
      .bytes = south ? provider->terrain.south : provider->terrain.dem,
      .byte_count =
        south ? provider->terrain.south_size : provider->terrain.dem_size,
    };
    mln_test_request_answer(provider->held[index], &response);
  }
}

// The clamped center rides the 100 m lowland, looking north, away from the
// step. A jump puts it 3 km south of the step, over a 1000 m plateau whose DEM
// tiles haven't arrived. Unknown ground isn't sea level, so the center keeps
// its height until they arrive, and then settles on the plateau.
static void a_jump_onto_ground_still_loading_keeps_the_center_height(void) {
  held_terrain_provider provider = {
    .terrain =
      {
        .dem = lowland_png,
        .dem_size = sizeof(lowland_png),
        .south = mln_test_plateau_1000_png,
        .south_size = mln_test_plateau_1000_png_size,
      },
    .target = {.latitude = step_latitude - 0.03, .longitude = 11.39},
    .lock = ATOMIC_FLAG_INIT,
    .holding = true,
  };
  const mln_resource_provider callback = {
    .size = sizeof(mln_resource_provider),
    .callback = serve_held_terrain,
    .user_data = &provider,
  };
  mln_runtime runtime = mln_test_create_runtime();
  MLN_TEST_OK(mln_test_set_resource_provider(runtime, &callback));
  mln_test_render_fixture fixture = {0};
  mln_map map = open_step_map(
    runtime, MLN_MAP_MODE_CONTINUOUS, step_latitude + 0.05, 14.5, 0.0, 12,
    &fixture
  );
  const mln_test_deadline settling = mln_test_deadline_default();
  while (fabs(read_camera(map).center_altitude - 100.0) > 1.0) {
    TEST_ASSERT_FALSE_MESSAGE(
      mln_test_deadline_passed(settling), "the center never met the lowland"
    );
    render_frame_snapshot(&fixture, map);
  }

  mln_camera_update jump = {
    .size = sizeof(mln_camera_update),
    .mode = MLN_CAMERA_UPDATE_MODE_JUMP,
    .camera = {
      .size = sizeof(mln_camera_options),
      .fields = MLN_CAMERA_OPTION_CENTER,
      .center = provider.target,
    },
  };
  MLN_TEST_AWAIT_OK(
    mln_map_update_camera(map, &jump, &completion.descriptor, NULL)
  );
  for (int frame = 0; frame < 10; ++frame) {
    const mln_map_snapshot snapshot = render_frame_snapshot(&fixture, map);
    TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(
      1.0, 100.0, snapshot.view.camera.center_altitude,
      "the center left its height before its ground loaded"
    );
  }

  release_held_terrain(&provider);
  mln_test_render_until_idle(runtime, map, &fixture);
  TEST_ASSERT_DOUBLE_WITHIN(1.0, 1000.0, read_camera(map).center_altitude);
  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

MLN_TEST_GROUP {
  RUN_TEST(the_load_mode_caps_the_tiles_a_deep_source_asks_for);
  RUN_TEST(anchoring_without_terrain_leaves_the_camera);
  RUN_TEST(anchoring_keeps_the_eye_and_raises_the_zoom);
  RUN_TEST(the_clamp_lifts_a_camera_out_of_the_ground);
  RUN_TEST(a_clamped_still_image_draws_the_ground_it_lands_on);
  RUN_TEST(anchoring_leaves_a_camera_in_the_ground);
  RUN_TEST(the_clamp_keeps_the_eye_above_the_ground_under_it);
  RUN_TEST(the_clamp_keeps_the_eye_above_a_sea_cliff);
  RUN_TEST(an_ease_onto_higher_ground_eases_the_center_up);
  RUN_TEST(a_jump_onto_ground_still_loading_keeps_the_center_height);
}
