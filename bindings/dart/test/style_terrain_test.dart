// The style's terrain, set and read back as JSON.
import 'package:test/test.dart';

import 'support/fixture.dart';

void main() {
  test('terrain round-trips as JSON, and null removes it', () async {
    final fixture = await openRuntime();
    final map = await fixture.openStyledMap();
    expect(await map.getStyleTerrainJson(), isNull);

    // The terrain may name a raster-dem source that the style lacks yet.
    await expectCommitted(
      map.setStyleTerrainJson({'source': 'dem', 'exaggeration': 1.25}),
    );
    expect(await map.getStyleTerrainJson(), {
      'source': 'dem',
      'exaggeration': 1.25,
    });

    await expectCommitted(map.setStyleTerrainJson(null));
    expect(await map.getStyleTerrainJson(), isNull);
  });
}
