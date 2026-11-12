#define MLN_BUILDING_C

#include "c_api/boundary.hpp"
#include "maplibre_native_c.h"
#include "render/render_session_common.hpp"

auto mln_render_session_query_rendered_features(
  mln_render_session session, const mln_rendered_query_geometry* geometry,
  const mln_rendered_feature_query_options* options,
  const mln_completion* completion, mln_diagnostic* out_diagnostic
) noexcept -> mln_status {
  return mln::c_api::status_boundary(out_diagnostic, [&]() -> mln_status {
    return mln::core::render_session_query_rendered_features_start(
      session, geometry, options, completion
    );
  });
}

auto mln_render_session_query_source_features(
  mln_render_session session, mln_buffer_view source_id,
  const mln_source_feature_query_options* options,
  const mln_completion* completion, mln_diagnostic* out_diagnostic
) noexcept -> mln_status {
  return mln::c_api::status_boundary(out_diagnostic, [&]() -> mln_status {
    return mln::core::render_session_query_source_features_start(
      session, source_id, options, completion
    );
  });
}

auto mln_render_session_get_cluster_children(
  mln_render_session session, mln_buffer_view source_id, uint32_t cluster_id,
  const mln_completion* completion, mln_diagnostic* out_diagnostic
) noexcept -> mln_status {
  return mln::c_api::status_boundary(out_diagnostic, [&]() -> mln_status {
    return mln::core::render_session_get_cluster_children_start(
      session, source_id, cluster_id, completion
    );
  });
}

auto mln_render_session_get_cluster_leaves(
  mln_render_session session, mln_buffer_view source_id, uint32_t cluster_id,
  uint32_t limit, uint32_t offset, const mln_completion* completion,
  mln_diagnostic* out_diagnostic
) noexcept -> mln_status {
  return mln::c_api::status_boundary(out_diagnostic, [&]() -> mln_status {
    return mln::core::render_session_get_cluster_leaves_start(
      session, source_id, cluster_id, limit, offset, completion
    );
  });
}

auto mln_render_session_get_cluster_expansion_zoom(
  mln_render_session session, mln_buffer_view source_id, uint32_t cluster_id,
  const mln_completion* completion, mln_diagnostic* out_diagnostic
) noexcept -> mln_status {
  return mln::c_api::status_boundary(out_diagnostic, [&]() -> mln_status {
    return mln::core::render_session_get_cluster_expansion_zoom_start(
      session, source_id, cluster_id, completion
    );
  });
}

auto mln_render_session_query_terrain_elevation(
  mln_render_session session, mln_lat_lng point,
  const mln_completion* completion, mln_diagnostic* out_diagnostic
) noexcept -> mln_status {
  return mln::c_api::status_boundary(out_diagnostic, [&]() -> mln_status {
    return mln::core::render_session_query_terrain_elevation_start(
      session, point, completion
    );
  });
}

auto mln_render_session_query_lat_lng_for_pixel(
  mln_render_session session, mln_screen_point point,
  const mln_completion* completion, mln_diagnostic* out_diagnostic
) noexcept -> mln_status {
  return mln::c_api::status_boundary(out_diagnostic, [&]() -> mln_status {
    return mln::core::render_session_query_lat_lng_for_pixel_start(
      session, point, completion
    );
  });
}

auto mln_render_session_query_pixel_for_lat_lng(
  mln_render_session session, mln_lat_lng coordinate,
  const mln_completion* completion, mln_diagnostic* out_diagnostic
) noexcept -> mln_status {
  return mln::c_api::status_boundary(out_diagnostic, [&]() -> mln_status {
    return mln::core::render_session_query_pixel_for_lat_lng_start(
      session, coordinate, completion
    );
  });
}

auto mln_render_session_query_pixels_for_lat_lngs(
  mln_render_session session, const mln_lat_lng* coordinates,
  size_t coordinate_count, const mln_completion* completion,
  mln_diagnostic* out_diagnostic
) noexcept -> mln_status {
  return mln::c_api::status_boundary(out_diagnostic, [&]() -> mln_status {
    return mln::core::render_session_query_pixels_for_lat_lngs_start(
      session, coordinates, coordinate_count, completion
    );
  });
}
