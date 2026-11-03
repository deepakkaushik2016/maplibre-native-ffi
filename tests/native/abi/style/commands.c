// Style command semantics shared by every style edit, and the style-wide values
// that belong to no layer.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "support/frames.h"
#include "support/map.h"
#include "support/resources.h"
#include "support/style.h"
#include "support/test_support.h"

static bool source_exists(mln_map map, const char* id) {
  mln_test_completion completion =
    mln_test_completion_default(sizeof(mln_style_source_info));
  MLN_TEST_OK(mln_map_get_style_source(
    map, mln_buffer_view_from_cstr(id), &completion.descriptor, NULL
  ));
  mln_style_source_info result;
  bool found = false;
  MLN_TEST_OK(mln_test_completion_finish_optional(
    &completion, &result, sizeof(result), &found
  ));
  return found;
}

static bool image_exists(mln_map map, const char* id) {
  mln_test_completion completion =
    mln_test_completion_default(sizeof(mln_style_image_info));
  MLN_TEST_OK(mln_map_get_style_image(
    map, mln_buffer_view_from_cstr(id), &completion.descriptor, NULL
  ));
  mln_style_image_info result;
  bool found = false;
  MLN_TEST_OK(mln_test_completion_finish_optional(
    &completion, &result, sizeof(result), &found
  ));
  return found;
}

static void style_commands_copy_inputs_and_fail_on_duplicate_or_missing_ids(
  void
) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_styled_map(runtime);
  char id[] = "owned-source";
  char json[sizeof(MLN_TEST_EMPTY_GEOJSON_SOURCE)];
  memcpy(json, MLN_TEST_EMPTY_GEOJSON_SOURCE, sizeof(json));
  mln_test_completion add = mln_test_completion_default(0);
  MLN_TEST_OK(mln_map_add_style_source_json(
    map, mln_buffer_view_from_cstr(id), mln_buffer_view_from_cstr(json),
    &add.descriptor, NULL
  ));
  // The command copied both inputs before it returned.
  memset(id, 'x', strlen(id));
  memset(json, ' ', strlen(json));
  MLN_TEST_OK(mln_test_completion_settle(&add));
  TEST_ASSERT_TRUE(source_exists(map, "owned-source"));

  const mln_buffer_view source = MLN_BUFFER_LITERAL("owned-source");
  const mln_buffer_view empty_source =
    MLN_BUFFER_LITERAL(MLN_TEST_EMPTY_GEOJSON_SOURCE);
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_ALREADY_EXISTS, "source \"owned-source\" already exists",
    mln_map_add_style_source_json(
      map, source, empty_source, &completion.descriptor, NULL
    )
  );
  MLN_TEST_AWAIT_OK(
    mln_map_remove_style_source(map, source, &completion.descriptor, NULL)
  );
  TEST_ASSERT_FALSE(source_exists(map, "owned-source"));

  const uint8_t pixel[4] = {0, 0, 0, 0};
  mln_premultiplied_rgba8_image image = {
    .size = sizeof(mln_premultiplied_rgba8_image)
  };
  image.width = 1;
  image.height = 1;
  image.stride = 4;
  image.pixels = pixel;
  image.byte_length = sizeof(pixel);
  const mln_buffer_view doomed = MLN_BUFFER_LITERAL("doomed-image");
  MLN_TEST_AWAIT_OK(mln_map_set_style_image(
    map, doomed, &image, NULL, &completion.descriptor, NULL
  ));
  MLN_TEST_AWAIT_OK(
    mln_map_remove_style_image(map, doomed, &completion.descriptor, NULL)
  );
  TEST_ASSERT_FALSE(image_exists(map, "doomed-image"));
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_NOT_FOUND, "style image \"doomed-image\" does not exist",
    mln_map_remove_style_image(map, doomed, &completion.descriptor, NULL)
  );

  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

static void missing_style_ids_report_not_found(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_styled_map(runtime);
  const mln_buffer_view layer = MLN_BUFFER_LITERAL("absent-layer");
  const mln_buffer_view source = MLN_BUFFER_LITERAL("absent-source");
  const mln_buffer_view none = MLN_BUFFER_LITERAL("");
  const char* missing_layer = "layer \"absent-layer\" does not exist";
  const char* missing_source = "source \"absent-source\" does not exist";

  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_NOT_FOUND, missing_layer,
    mln_map_remove_style_layer(map, layer, &completion.descriptor, NULL)
  );
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_NOT_FOUND, missing_layer,
    mln_map_move_style_layer(map, layer, none, &completion.descriptor, NULL)
  );
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_NOT_FOUND, missing_layer,
    mln_map_set_style_layer_property(
      map, layer, MLN_BUFFER_LITERAL("circle-radius"), MLN_BUFFER_LITERAL("4"),
      &completion.descriptor, NULL
    )
  );
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_NOT_FOUND, "before_layer_id \"absent-layer\" does not exist",
    mln_map_add_style_layer_json(
      map,
      MLN_BUFFER_LITERAL(
        "{\"id\":\"indicator\",\"type\":\"location-indicator\"}"
      ),
      layer, &completion.descriptor, NULL
    )
  );
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_NOT_FOUND,
    "layer \"hillshade\" uses source \"absent-source\", which does not exist",
    mln_map_add_style_layer_json(
      map,
      MLN_BUFFER_LITERAL(
        "{\"id\":\"hillshade\",\"type\":\"hillshade\",\"source\":\"absent-"
        "source\"}"
      ),
      none, &completion.descriptor, NULL
    )
  );
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_NOT_FOUND, missing_source,
    mln_map_remove_style_source(map, source, &completion.descriptor, NULL)
  );
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_NOT_FOUND, missing_source,
    mln_map_set_image_source_url(
      map, source, MLN_BUFFER_LITERAL("fixture://image.png"),
      &completion.descriptor, NULL
    )
  );
  const mln_lat_lng corners[4] = {{1, 0}, {1, 1}, {0, 1}, {0, 0}};
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_NOT_FOUND, missing_source,
    mln_map_set_image_source_coordinates(
      map, source, corners, 4, &completion.descriptor, NULL
    )
  );
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_NOT_FOUND, missing_source,
    mln_map_set_geo_json_source_url(
      map, source, MLN_BUFFER_LITERAL("fixture://points.geojson"),
      &completion.descriptor, NULL
    )
  );
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_NOT_FOUND, missing_source,
    mln_map_set_geo_json_source_synchronous_tiling(
      map, source, true, &completion.descriptor, NULL
    )
  );
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_NOT_FOUND, missing_source,
    mln_map_set_style_source_volatile(
      map, source, true, &completion.descriptor, NULL
    )
  );
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_NOT_FOUND, missing_source,
    mln_map_invalidate_custom_geometry_source_region(
      map, source, (mln_lat_lng_bounds){{-1, -1}, {1, 1}},
      &completion.descriptor, NULL
    )
  );
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_NOT_FOUND, missing_source,
    mln_map_invalidate_custom_vector_source_region(
      map, source, (mln_lat_lng_bounds){{-1, -1}, {1, 1}},
      &completion.descriptor, NULL
    )
  );

  // A query of one attribute of a missing layer completes with
  // MLN_STATUS_NOT_FOUND too.
  MLN_TEST_AWAIT_COMMAND(
    MLN_STATUS_NOT_FOUND, mln_map_get_style_layer_property(
                            map, layer, MLN_BUFFER_LITERAL("circle-radius"),
                            &completion.descriptor, NULL
                          )
  );
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

static void read_global_state(mln_map map, char* out, size_t capacity) {
  mln_test_completion completion = mln_test_completion_buffer_view();
  MLN_TEST_OK(mln_map_get_global_state(map, &completion.descriptor, NULL));
  bool found = false;
  MLN_TEST_OK(mln_test_style_finish_text(&completion, out, capacity, &found));
  TEST_ASSERT_TRUE(found);
}

static void set_global_state(mln_map map, const char* value) {
  MLN_TEST_AWAIT_OK(mln_map_set_global_state_property(
    map, MLN_BUFFER_LITERAL("theme"), mln_buffer_view_from_cstr(value),
    &completion.descriptor, NULL
  ));
}

#define EXPECT_GLOBAL_STATE(expected, map)          \
  do {                                              \
    char state[128];                                \
    read_global_state((map), state, sizeof(state)); \
    TEST_ASSERT_EQUAL_STRING((expected), state);    \
  } while (false)

static void global_state_reads_back_defaults_and_set_properties(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_completion discard = mln_test_discard_completion();
  MLN_TEST_INVALID(mln_map_get_global_state(map, NULL, NULL));
  MLN_TEST_INVALID(mln_map_set_global_state_property(
    map, (mln_buffer_view){.data = NULL, .size = 1}, MLN_BUFFER_LITERAL("true"),
    &discard, NULL
  ));
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_INVALID_STATE, "style is not loaded",
    mln_map_set_global_state_property(
      map, MLN_BUFFER_LITERAL("theme"), MLN_BUFFER_LITERAL("true"),
      &completion.descriptor, NULL
    )
  );

  static const char stateful_style[] =
    "{\"version\":8,\"sources\":{},\"layers\":[],"
    "\"state\":{\"theme\":{\"default\":\"light\"}}}";
  mln_test_load_style_and_wait(map, MLN_BUFFER_LITERAL(stateful_style));
  EXPECT_GLOBAL_STATE("{\"theme\":\"light\"}", map);
  set_global_state(map, "[\"dark\",{\"enabled\":true}]");
  EXPECT_GLOBAL_STATE("{\"theme\":[\"dark\",{\"enabled\":true}]}", map);
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_INVALID_ARGUMENT, "",
    mln_map_set_global_state_property(
      map, MLN_BUFFER_LITERAL("theme"), MLN_BUFFER_LITERAL("{not json"),
      &completion.descriptor, NULL
    )
  );
  EXPECT_GLOBAL_STATE("{\"theme\":[\"dark\",{\"enabled\":true}]}", map);
  set_global_state(map, "null");
  EXPECT_GLOBAL_STATE("{\"theme\":\"light\"}", map);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

static void read_loaded_style_json(mln_map map, char* out, size_t capacity) {
  mln_test_completion completion = mln_test_completion_buffer_view();
  MLN_TEST_OK(mln_map_get_loaded_style_json(map, &completion.descriptor, NULL));
  bool found = false;
  MLN_TEST_OK(mln_test_style_finish_text(&completion, out, capacity, &found));
  TEST_ASSERT_TRUE(found);
}

static void the_loaded_style_document_reads_back_as_loaded(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  char document[512];
  read_loaded_style_json(map, document, sizeof(document));
  TEST_ASSERT_EQUAL_STRING("", document);

  static const char named_style[] =
    "{\"version\":8,\"name\":\"loaded\",\"sources\":{},\"layers\":[]}";
  mln_test_load_style_and_wait(map, MLN_BUFFER_LITERAL(named_style));
  MLN_TEST_AWAIT_OK(mln_map_add_style_source_json(
    map, MLN_BUFFER_LITERAL("added-later"),
    MLN_BUFFER_LITERAL(MLN_TEST_EMPTY_GEOJSON_SOURCE), &completion.descriptor,
    NULL
  ));
  read_loaded_style_json(map, document, sizeof(document));
  TEST_ASSERT_EQUAL_STRING(named_style, document);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

static mln_style_transition_options read_transition_options(mln_map map) {
  mln_test_completion completion =
    mln_test_completion_default(sizeof(mln_style_transition_options));
  MLN_TEST_OK(
    mln_map_get_style_transition_options(map, &completion.descriptor, NULL)
  );
  mln_style_transition_options value = {0};
  MLN_TEST_OK(
    mln_test_completion_finish_value(&completion, &value, sizeof(value))
  );
  return value;
}

static void set_transition_options(
  mln_map map, const mln_style_transition_options* options
) {
  MLN_TEST_AWAIT_OK(mln_map_set_style_transition_options(
    map, options, &completion.descriptor, NULL
  ));
}

static const uint32_t timing =
  MLN_STYLE_TRANSITION_OPTION_DURATION | MLN_STYLE_TRANSITION_OPTION_DELAY;

// MapLibre picks the default duration, so the case checks only which fields
// the read reports.
static void transition_options_report_the_fields_the_style_sets(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  TEST_ASSERT_EQUAL_UINT32(0, read_transition_options(map).fields & timing);

  mln_test_load_style_and_wait(
    map, MLN_BUFFER_LITERAL(
           "{\"version\":8,\"transition\":{\"delay\":50},\"sources\":{},"
           "\"layers\":[]}"
         )
  );
  const mln_style_transition_options read = read_transition_options(map);
  TEST_ASSERT_EQUAL_UINT32(
    MLN_STYLE_TRANSITION_OPTION_DELAY, read.fields & timing
  );
  TEST_ASSERT_EQUAL_DOUBLE(50.0, read.delay_ms);

  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

// Raw callers can hand the transition setter a struct no binding would build.
static void style_transition_options_reject_unsafe_raw_input(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_styled_map(runtime);

  mln_style_transition_options applied = {
    .size = sizeof(mln_style_transition_options)
  };
  applied.fields = timing;
  applied.duration_ms = 250.0;
  applied.delay_ms = 75.0;
  set_transition_options(map, &applied);
  mln_style_transition_options read = read_transition_options(map);
  TEST_ASSERT_EQUAL_DOUBLE(250.0, read.duration_ms);
  TEST_ASSERT_EQUAL_DOUBLE(75.0, read.delay_ms);
  TEST_ASSERT_TRUE(read.enable_placement_transitions);

  mln_style_transition_options placement = {
    .size = sizeof(mln_style_transition_options)
  };
  placement.fields = MLN_STYLE_TRANSITION_OPTION_ENABLE_PLACEMENT_TRANSITIONS;
  placement.enable_placement_transitions = false;
  set_transition_options(map, &placement);
  read = read_transition_options(map);
  TEST_ASSERT_BITS_HIGH(placement.fields, read.fields);
  TEST_ASSERT_FALSE(read.enable_placement_transitions);
  set_transition_options(map, &applied);

  MLN_TEST_EXPECT_COMMAND_REJECTED(
    "options must not be null",
    mln_map_set_style_transition_options(
      map, NULL, &completion.descriptor, MLN_TEST_DIAGNOSTIC
    )
  );
  mln_style_transition_options rejected = applied;
  rejected.size = sizeof(mln_style_transition_options) - 1;
  MLN_TEST_EXPECT_COMMAND_REJECTED(
    "size is too small",
    mln_map_set_style_transition_options(
      map, &rejected, &completion.descriptor, MLN_TEST_DIAGNOSTIC
    )
  );

  rejected = applied;
  rejected.fields |= UINT32_C(1) << 31;
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_INVALID_ARGUMENT, "unknown bits",
    mln_map_set_style_transition_options(
      map, &rejected, &completion.descriptor, NULL
    )
  );
  rejected = applied;
  rejected.duration_ms = -1.0;
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_INVALID_ARGUMENT, "duration_ms",
    mln_map_set_style_transition_options(
      map, &rejected, &completion.descriptor, NULL
    )
  );
  rejected = applied;
  rejected.delay_ms = INFINITY;
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_INVALID_ARGUMENT, "delay_ms",
    mln_map_set_style_transition_options(
      map, &rejected, &completion.descriptor, NULL
    )
  );

  read = read_transition_options(map);
  TEST_ASSERT_EQUAL_DOUBLE(250.0, read.duration_ms);
  TEST_ASSERT_EQUAL_DOUBLE(75.0, read.delay_ms);

  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

// Expects one light property's JSON, or "" when it is undefined.
#define EXPECT_LIGHT_PROPERTY(expected, map, name)                    \
  do {                                                                \
    mln_test_completion light = mln_test_completion_buffer_view();    \
    MLN_TEST_OK(mln_map_get_style_light_property(                     \
      (map), MLN_BUFFER_LITERAL(name), &light.descriptor, NULL        \
    ));                                                               \
    char value[64];                                                   \
    MLN_TEST_OK(mln_test_style_finish_text(&light, value, 64, NULL)); \
    TEST_ASSERT_EQUAL_STRING((expected), value);                      \
  } while (false)

// The light has no ID to miss, so every bad input is an INVALID_ARGUMENT.
static void the_style_light_round_trips_through_json(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_empty_style_json);
  const mln_buffer_view intensity = MLN_BUFFER_LITERAL("intensity");

  MLN_TEST_AWAIT_OK(mln_map_set_style_light_json(
    map, MLN_BUFFER_LITERAL("{\"anchor\":\"viewport\",\"intensity\":0.5}"),
    &completion.descriptor, NULL
  ));
  EXPECT_LIGHT_PROPERTY("\"viewport\"", map, "anchor");
  EXPECT_LIGHT_PROPERTY("0.5", map, "intensity");

  MLN_TEST_AWAIT_OK(mln_map_set_style_light_property(
    map, intensity, MLN_BUFFER_LITERAL("0.75"), &completion.descriptor, NULL
  ));
  EXPECT_LIGHT_PROPERTY("0.75", map, "intensity");

  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_INVALID_ARGUMENT, "style light property",
    mln_map_set_style_light_property(
      map, intensity, MLN_BUFFER_LITERAL("\"bright\""), &completion.descriptor,
      NULL
    )
  );
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_INVALID_ARGUMENT, "style light",
    mln_map_set_style_light_json(
      map, MLN_BUFFER_LITERAL("{\"anchor\":7}"), &completion.descriptor, NULL
    )
  );
  EXPECT_LIGHT_PROPERTY("0.75", map, "intensity");
  EXPECT_LIGHT_PROPERTY("\"viewport\"", map, "anchor");
  EXPECT_LIGHT_PROPERTY("", map, "brightness");

  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

static void read_terrain(mln_map map, char* out, size_t capacity) {
  mln_test_completion completion = mln_test_completion_buffer_view();
  MLN_TEST_OK(
    mln_map_get_style_terrain_json(map, &completion.descriptor, NULL)
  );
  bool found = false;
  MLN_TEST_OK(mln_test_style_finish_text(&completion, out, capacity, &found));
  TEST_ASSERT_TRUE(found);
}

#define EXPECT_TERRAIN(expected, map)              \
  do {                                             \
    char terrain[128];                             \
    read_terrain((map), terrain, sizeof(terrain)); \
    TEST_ASSERT_EQUAL_STRING((expected), terrain); \
  } while (false)

// The terrain has no ID to miss, so every bad input is an INVALID_ARGUMENT,
// and JSON null is how a caller turns it off.
static void the_style_terrain_round_trips_through_json(void) {
  mln_runtime runtime = mln_test_create_runtime();
  mln_map map = mln_test_create_map(runtime);
  mln_test_load_style_and_wait(map, mln_test_empty_style_json);
  EXPECT_TERRAIN("null", map);

  MLN_TEST_AWAIT_OK(mln_map_set_style_terrain_json(
    map, MLN_BUFFER_LITERAL("{\"source\":\"dem\",\"exaggeration\":1.25}"),
    &completion.descriptor, NULL
  ));
  EXPECT_TERRAIN("{\"source\":\"dem\",\"exaggeration\":1.25}", map);

  MLN_TEST_EXPECT_COMMAND_REJECTED(
    "terrain_json",
    mln_map_set_style_terrain_json(
      map, MLN_BUFFER_LITERAL(""), &completion.descriptor, MLN_TEST_DIAGNOSTIC
    )
  );
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_INVALID_ARGUMENT, "terrain",
    mln_map_set_style_terrain_json(
      map, MLN_BUFFER_LITERAL("{\"source\":"), &completion.descriptor, NULL
    )
  );
  MLN_TEST_EXPECT_COMMAND_FAILED(
    MLN_STATUS_INVALID_ARGUMENT, "terrain",
    mln_map_set_style_terrain_json(
      map, MLN_BUFFER_LITERAL("{\"exaggeration\":2}"), &completion.descriptor,
      NULL
    )
  );
  // A rejected terrain leaves the one before it in place.
  EXPECT_TERRAIN("{\"source\":\"dem\",\"exaggeration\":1.25}", map);

  MLN_TEST_AWAIT_OK(mln_map_set_style_terrain_json(
    map, MLN_BUFFER_LITERAL("null"), &completion.descriptor, NULL
  ));
  EXPECT_TERRAIN("null", map);

  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

// A 32 by 32 Terrarium PNG: a cone that rises from 0 m at its rim to 2048 m at
// the tile's center, in 256 m steps.
static const uint8_t dem_png[] = {
  0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49,
  0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x08, 0x02,
  0x00, 0x00, 0x00, 0xfc, 0x18, 0xed, 0xa3, 0x00, 0x00, 0x00, 0xaf, 0x49, 0x44,
  0x41, 0x54, 0x78, 0xda, 0xd5, 0x96, 0x4b, 0x0e, 0x80, 0x20, 0x0c, 0x44, 0x39,
  0xba, 0x0a, 0x7e, 0x8e, 0x6d, 0xe2, 0xaa, 0xa1, 0x33, 0xa5, 0x51, 0xda, 0xc4,
  0xe4, 0x2d, 0x75, 0x1e, 0x14, 0xa5, 0x2d, 0x4b, 0x29, 0xa1, 0xb8, 0x1e, 0x5a,
  0x09, 0x5f, 0x05, 0xab, 0x9b, 0x37, 0x02, 0xf9, 0xfe, 0x46, 0xf0, 0x38, 0x5e,
  0x46, 0xfb, 0x35, 0x34, 0xbd, 0x4b, 0xa9, 0x0a, 0xa6, 0xb1, 0x04, 0x3a, 0xbd,
  0x3a, 0xb0, 0x1d, 0x40, 0x00, 0xd3, 0x9b, 0xc2, 0x70, 0x60, 0x01, 0x4b, 0x6f,
  0x26, 0x43, 0x07, 0x2e, 0x8e, 0x4e, 0xdf, 0x15, 0xcc, 0xd1, 0x15, 0x0a, 0x2c,
  0xdf, 0x93, 0xee, 0x71, 0x0c, 0x04, 0x3a, 0xfd, 0x10, 0x30, 0x07, 0x16, 0xc0,
  0xea, 0xcb, 0xf4, 0x83, 0x20, 0x1d, 0xec, 0x24, 0xac, 0xe5, 0x77, 0x6b, 0x3f,
  0x05, 0x70, 0x1f, 0x70, 0x13, 0x63, 0x81, 0x4e, 0xd7, 0x8e, 0x69, 0x82, 0xeb,
  0xe1, 0xcf, 0x82, 0x90, 0x12, 0x4d, 0x3e, 0xe4, 0x90, 0xcf, 0x34, 0xfc, 0x47,
  0xcb, 0xbb, 0x2a, 0x02, 0x2f, 0xbb, 0xf0, 0xeb, 0x3a, 0xa3, 0xe1, 0x64, 0xb4,
  0xcc, 0xf0, 0xa6, 0x9f, 0x31, 0xb6, 0x64, 0x0c, 0x5e, 0x49, 0xa3, 0x63, 0xc6,
  0xf0, 0x3b, 0x85, 0x1b, 0x25, 0x22, 0x08, 0x97, 0x77, 0xcb, 0x0f, 0xba, 0x00,
  0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

static void serve_dem_tile(
  void* user_data, const mln_resource_request_info* request,
  mln_resource_request handle
) {
  (void)user_data;
  (void)request;
  const mln_resource_response response = {
    .size = sizeof(mln_resource_response),
    .status = MLN_RESOURCE_RESPONSE_STATUS_OK,
    .error_reason = MLN_RESOURCE_ERROR_REASON_NONE,
    .bytes = dem_png,
    .byte_count = sizeof(dem_png),
  };
  mln_test_request_answer(handle, &response);
}

// A pitched static map over a grid of lines, which terrain drapes over the
// cones in the DEM tiles.
static mln_map create_terrain_grid_map(mln_runtime runtime) {
  mln_map_options options = mln_test_map_options();
  options.initial_extent =
    (mln_logical_extent){.width = 64, .height = 64, .scale_factor = 1.0};
  options.map_mode = MLN_MAP_MODE_STATIC;
  mln_map map = mln_test_create_map_with_options(runtime, &options);

  enum { style_capacity = 16384 };
  char* style = malloc(style_capacity);
  TEST_ASSERT_NOT_NULL(style);
  int length = snprintf(
    style, style_capacity,
    "{\"version\":8,\"center\":[31.025390625,36.527295],\"zoom\":10,"
    "\"pitch\":60,\"sources\":{\"grid\":{\"type\":\"geojson\",\"data\":"
    "{\"type\":\"MultiLineString\",\"coordinates\":["
  );
  for (int line = 0; line <= 50; line += 1) {
    const double lng = 30.9 + (line * 0.005);
    const double lat = 36.4 + (line * 0.005);
    length += snprintf(
      style + length, (size_t)(style_capacity - length),
      "%s[[%.3f,36.4],[%.3f,36.65]],[[30.9,%.3f],[31.15,%.3f]]",
      line == 0 ? "" : ",", lng, lng, lat, lat
    );
  }
  length += snprintf(
    style + length, (size_t)(style_capacity - length),
    "]}}},\"layers\":[{\"id\":\"bg\",\"type\":\"background\",\"paint\":"
    "{\"background-color\":\"#ffffff\"}},{\"id\":\"grid\",\"type\":\"line\","
    "\"source\":\"grid\",\"paint\":{\"line-color\":\"#000000\","
    "\"line-width\":2}}]}"
  );
  TEST_ASSERT_TRUE(length > 0 && length < style_capacity);
  mln_test_load_style_and_wait(map, mln_buffer_view_from_cstr(style));
  free(style);
  return map;
}

enum { terrain_pixel_bytes = 64 * 64 * 4 };

static void render_terrain_grid(
  const mln_test_render_fixture* fixture, mln_map map, uint8_t* out_pixels
) {
  MLN_TEST_OK(mln_test_render_still_image(fixture, map));
  mln_texture_image_info info = {0};
  MLN_TEST_OK(
    mln_test_render_read_back(fixture, &info, out_pixels, terrain_pixel_bytes)
  );
  TEST_ASSERT_EQUAL_size_t(terrain_pixel_bytes, info.byte_length);
}

static size_t count_changed_pixels(
  const uint8_t* before, const uint8_t* after
) {
  size_t changed = 0;
  for (size_t pixel = 0; pixel < terrain_pixel_bytes; pixel += 4) {
    if (memcmp(before + pixel, after + pixel, 4) != 0) {
      changed += 1;
    }
  }
  return changed;
}

// Terrain may name a source the style doesn't have yet. Once the source
// arrives, the map lifts the grid onto the cone that its DEM tiles hold, which
// bends the grid's lines on screen where a terrain with no exaggeration leaves
// them straight.
static void terrain_set_before_its_source_applies_once_the_source_arrives(
  void
) {
  const mln_resource_provider provider = {
    .size = sizeof(mln_resource_provider),
    .callback = serve_dem_tile,
  };
  mln_runtime runtime = mln_test_create_runtime();
  MLN_TEST_OK(mln_test_set_resource_provider(runtime, &provider));
  mln_map map = create_terrain_grid_map(runtime);
  mln_test_render_fixture fixture = {0};
  TEST_ASSERT_TRUE(mln_test_render_fixture_create_offscreen(map, &fixture));

  MLN_TEST_AWAIT_OK(mln_map_set_style_terrain_json(
    map, MLN_BUFFER_LITERAL("{\"source\":\"dem\",\"exaggeration\":1.25}"),
    &completion.descriptor, NULL
  ));
  MLN_TEST_AWAIT_OK(mln_map_add_style_source_json(
    map, MLN_BUFFER_LITERAL("dem"),
    MLN_BUFFER_LITERAL(
      "{\"type\":\"raster-dem\",\"tiles\":[\"custom://dem/{z}/{x}/{y}.png\"],"
      "\"tileSize\":256,\"encoding\":\"terrarium\",\"maxzoom\":11}"
    ),
    &completion.descriptor, NULL
  ));
  static uint8_t raised[terrain_pixel_bytes];
  render_terrain_grid(&fixture, map, raised);
  EXPECT_TERRAIN("{\"source\":\"dem\",\"exaggeration\":1.25}", map);

  MLN_TEST_AWAIT_OK(mln_map_set_style_terrain_json(
    map, MLN_BUFFER_LITERAL("{\"source\":\"dem\",\"exaggeration\":0}"),
    &completion.descriptor, NULL
  ));
  static uint8_t level[terrain_pixel_bytes];
  render_terrain_grid(&fixture, map, level);
  TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(
    (size_t)(64 * 64) / 4, count_changed_pixels(level, raised),
    "the terrain left the grid flat"
  );

  mln_test_render_fixture_destroy(&fixture);
  mln_test_destroy_map(map);
  mln_test_destroy_runtime(runtime);
}

MLN_TEST_GROUP {
  RUN_TEST(style_commands_copy_inputs_and_fail_on_duplicate_or_missing_ids);
  RUN_TEST(missing_style_ids_report_not_found);
  RUN_TEST(global_state_reads_back_defaults_and_set_properties);
  RUN_TEST(the_loaded_style_document_reads_back_as_loaded);
  RUN_TEST(transition_options_report_the_fields_the_style_sets);
  RUN_TEST(style_transition_options_reject_unsafe_raw_input);
  RUN_TEST(the_style_light_round_trips_through_json);
  RUN_TEST(the_style_terrain_round_trips_through_json);
  RUN_TEST(terrain_set_before_its_source_applies_once_the_source_arrives);
}
