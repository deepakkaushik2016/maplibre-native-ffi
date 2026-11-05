// The conversions between screen points and the ground that a render
// session's last frame drew.
import 'dart:convert';

import 'package:maplibre_native_ffi/maplibre_native_ffi.dart';
import 'package:test/test.dart';

import 'support/fixture.dart';
import 'support/render.dart';

/// A 32 by 32 Mapbox Terrain-RGB PNG that decodes to 1000 m everywhere, the
/// plateau of the native terrain suites.
final _plateauPng = base64Decode(
  'iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAIAAAD8GO2jAAAAKklEQVR42u3NQQkAAAgEsLN/FsGIpvA'
  'hDPZfqudUBAKBQCAQCAQCwZdgAcFaeEyURzhvAAAAAElFTkSuQmCC',
);

const _plateau = LatLng(latitude: 47.27, longitude: 11.39);

final _plateauStyle = {
  'version': 8,
  'center': [_plateau.longitude, _plateau.latitude],
  'zoom': 10,
  'sources': {
    'dem': {
      'type': 'raster-dem',
      'tiles': ['custom://dem/{z}/{x}/{y}.png'],
      'encoding': 'mapbox',
      'tileSize': 256,
      'maxzoom': 12,
    },
  },
  'terrain': {'source': 'dem', 'exaggeration': 1},
  'layers': <Object?>[],
};

/// Attaches a session to a static map of the plateau, which renders no frame
/// until it is asked for a still image.
Future<(MapHandle, WorkerSession)> _openPlateau() async {
  final fixture = await openRuntime(
    provider: ResourceProvider(
      callback: (request, handle) => handle.complete(
        request.requestedUrl.contains('/dem/')
            ? ResourceResponse(bytes: _plateauPng)
            : ResourceResponse(status: ResourceResponseStatus.noContent),
      ),
    ),
  );
  final map = await fixture.openMap(
    MapOptions(
      mapMode: MapMode.static,
      initialExtent: LogicalExtent(
        width: renderSize,
        height: renderSize,
        scaleFactor: 1,
      ),
    ),
  );
  await expectCommitted(map.setStyleJson(_plateauStyle));
  return (map, await WorkerSession.attach(map));
}

void main() {
  test('the ground under a point is drawn back at that point', () async {
    final (map, worker) = await _openPlateau();
    await within(map.requestStillImage(), 'the still image');

    const middle = ScreenPoint(x: renderSize / 2, y: renderSize / 2);
    final ground = await within(
      worker.session.queryLatLngForPixel(middle),
      'the coordinate query',
    );
    expect(ground?.coordinate.latitude, closeTo(_plateau.latitude, 1e-6));
    expect(ground?.coordinate.longitude, closeTo(_plateau.longitude, 1e-6));

    final drawn = await within(
      worker.session.queryPixelForLatLng(ground!.coordinate),
      'the pixel query',
    );
    expect(drawn?.point.x, closeTo(middle.x, 0.01));
    expect(drawn?.point.y, closeTo(middle.y, 0.01));
    expect(drawn?.occluded, isFalse);

    // Half a world away, the view loaded no DEM tile.
    final elsewhere = await within(
      worker.session.queryPixelForLatLng(
        const LatLng(latitude: -33.86, longitude: -168.79),
      ),
      'the pixel query',
    );
    expect(elsewhere, isNull);
  });

  // Null means the frame drew no ground there. Before the first frame there is
  // no frame to read, so the queries throw rather than completing with null.
  test('a session that never rendered throws invalidState', () async {
    final (_, worker) = await _openPlateau();
    final invalidState = throwsA(
      isA<MaplibreException>().having(
        (error) => error.status,
        'status',
        Status.invalidState,
      ),
    );

    await expectLater(
      worker.session.queryLatLngForPixel(const ScreenPoint(x: 16, y: 16)),
      invalidState,
    );
    await expectLater(
      worker.session.queryPixelForLatLng(_plateau),
      invalidState,
    );
  });
}
