#ifndef MLN_NATIVE_TESTS_TERRAIN_H
#define MLN_NATIVE_TESTS_TERRAIN_H

// DEM tiles for the terrain suites, and a resource provider that serves one.

#include <stddef.h>
#include <stdint.h>

#include "maplibre_native_c.h"

#ifdef __cplusplus
extern "C" {
#endif

// 32 by 32 Mapbox Terrain-RGB PNGs that decode to one height everywhere:
// 1000 m and 1600 m.
extern const uint8_t mln_test_plateau_1000_png[];
extern const size_t mln_test_plateau_1000_png_size;
extern const uint8_t mln_test_plateau_1600_png[];
extern const size_t mln_test_plateau_1600_png_size;

// A 32 by 32 Mapbox Terrain-RGB PNG of an east-west ridge along the tile's
// middle row: 500 m at the tile's northern and southern edges, rising to
// 1484.5 m on the two middle rows, so neighboring tiles join without a step.
// Served at z11, it puts a crest every 13 km at 47 N.
extern const uint8_t mln_test_ridge_png[];
extern const size_t mln_test_ridge_png_size;

// The encoded bytes of one DEM tile.
typedef struct mln_test_dem_tile {
  const uint8_t* bytes;
  size_t size;
} mln_test_dem_tile;

// A provider that answers every request for a URL holding "/dem/" with
// `tile`, and every other request with no content. The tile must outlive the
// runtime that uses the provider.
mln_resource_provider mln_test_dem_provider(const mln_test_dem_tile* tile);

#ifdef __cplusplus
}
#endif

#endif
