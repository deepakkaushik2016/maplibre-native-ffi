// Generated from the C headers by tools/bindgen. Do not edit.
#nullable enable
namespace Maplibre.NativeFfi;

/// <summary>
/// All the settings for how a map loads its style's terrain.
/// </summary>
/// <remarks>
/// <para>Its members mean the same as those of <c>MapTerrainOptions</c>.</para>
/// <para>See <c>mln_map_terrain_settings</c> in the <see
/// href="https://maplibre.org/maplibre-native-ffi/reference/c/map_8h.html">C API reference</see>.</para>
/// </remarks>
public readonly partial record struct MapTerrainSettings(
    TerrainLoadMode LoadMode,
    bool CenterClampedToGround
);
