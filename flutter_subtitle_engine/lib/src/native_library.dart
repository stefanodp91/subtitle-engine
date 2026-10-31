import 'dart:ffi';
import 'dart:io';

/// Opens the engine: [path] on the Mac (tests), `libsubtitle_engine.so` from
/// the host app's native libraries on Android.
DynamicLibrary openSubtitleEngine(String? path) {
  if (path != null) return DynamicLibrary.open(path);
  if (Platform.isAndroid) return DynamicLibrary.open('libsubtitle_engine.so');
  throw UnsupportedError('No subtitle engine for ${Platform.operatingSystem}: pass libraryPath');
}
