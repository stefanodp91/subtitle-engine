import 'dart:async';
import 'dart:convert';
import 'dart:ffi';
import 'dart:isolate';

import 'package:ffi/ffi.dart';

import 'bindings_generated.dart';
import 'models.dart';
import 'native_library.dart';

/// The native engine behind a dedicated isolate: native pointers live only
/// there, calls queue up in order and never block the caller's isolate.
class SubtitleEngine {
  SubtitleEngine._(this._isolate, this._commands, this._replies);

  final Isolate _isolate;
  final SendPort _commands;
  final ReceivePort _replies;
  final _pending = <int, Completer<Object?>>{};
  int _nextRequest = 0;

  /// Starts the isolate and loads the library ([libraryPath] on the Mac).
  static Future<SubtitleEngine> start({String? libraryPath}) async {
    final replies = ReceivePort();
    final isolate = await Isolate.spawn(_workerMain, (replies.sendPort, libraryPath));
    final firstReply = Completer<SendPort>();
    late final SubtitleEngine engine;
    replies.listen((message) {
      if (message is SendPort) {
        firstReply.complete(message);
        return;
      }
      engine._onReply(message as List<Object?>);
    });
    engine = SubtitleEngine._(isolate, await firstReply.future, replies);
    return engine;
  }

  Future<TranslatorHandle> openTranslator(List<TranslatorModelFiles> models, {int? workspaceMb}) async {
    final spec = {
      'models': [for (final m in models) m.toJson()],
      if (workspaceMb != null) 'workspace_mb': workspaceMb,
    };
    return TranslatorHandle(await _call('open', [jsonEncode(spec)]) as int);
  }

  Future<List<String>> translate(TranslatorHandle translator, List<String> lines) async {
    final out = await _call('translate', [translator.id, jsonEncode(lines)]) as String;
    return (jsonDecode(out) as List).cast<String>();
  }

  Future<void> closeTranslator(TranslatorHandle translator) => _call('close', [translator.id]);

  Future<List<MkvTextTrack>> mkvTextTracks(String path) async {
    final out = await _call('tracks', [path]) as String;
    return [for (final t in jsonDecode(out) as List) MkvTextTrack.fromJson(t as Map<String, dynamic>)];
  }

  Future<MkvExtraction> extractMkvTrack(String path, int trackNumber) async {
    final json = jsonDecode(await _call('extract', [path, trackNumber]) as String) as Map<String, dynamic>;
    return MkvExtraction(
      cues: [for (final c in json['cues'] as List) SubtitleCue.fromJson(c as Map<String, dynamic>)],
      complete: json['complete'] as bool,
    );
  }

  /// Decompresses the .zst file [inPath] into [outPath]; fails with code
  /// io_error, corrupt_data or too_large (over [maxBytes]), leaving no output.
  Future<void> decompressZstd(String inPath, String outPath, {required int maxBytes}) =>
      _call('zstd', [inPath, outPath, maxBytes]);

  /// Closes the translators still open and stops the isolate.
  Future<void> dispose() async {
    await _call('dispose', const []);
    _replies.close();
    _isolate.kill();
  }

  Future<Object?> _call(String command, List<Object?> args) {
    final id = _nextRequest++;
    final completer = Completer<Object?>();
    _pending[id] = completer;
    _commands.send([id, command, ...args]);
    return completer.future;
  }

  void _onReply(List<Object?> reply) {
    final completer = _pending.remove(reply[0] as int);
    if (completer == null) return;
    if (reply[1] == true) {
      completer.complete(reply[2]);
    } else {
      completer.completeError(SubtitleEngineException(reply[2] as String, reply[3] as String));
    }
  }
}

// ---- The isolate side --------------------------------------------------------

class _EngineError implements Exception {
  _EngineError(this.code, this.message);

  final String code;
  final String message;
}

void _workerMain((SendPort, String?) init) {
  final (replies, libraryPath) = init;
  final commands = ReceivePort();
  replies.send(commands.sendPort);
  final bindings = SubtitleEngineBindings(openSubtitleEngine(libraryPath));
  final translators = <int, Pointer<se_translator>>{};
  var nextTranslator = 1;

  // Runs one C call that returns a string (or a translator), turning error_json into _EngineError.
  T native<T>(T Function(Pointer<Pointer<Char>> error) call, bool Function(T) failed) {
    final error = calloc<Pointer<Char>>();
    try {
      final result = call(error);
      if (failed(result)) {
        final raw = error.value;
        final json = raw == nullptr
            ? const {'code': 'invalid_argument', 'message': 'unknown error'}
            : jsonDecode(raw.cast<Utf8>().toDartString()) as Map<String, dynamic>;
        if (raw != nullptr) bindings.se_free(raw);
        throw _EngineError(json['code'] as String, json['message'] as String);
      }
      return result;
    } finally {
      calloc.free(error);
    }
  }

  String takeString(Pointer<Char> Function(Pointer<Pointer<Char>> error) call) {
    final out = native(call, (Pointer<Char> p) => p == nullptr);
    try {
      return out.cast<Utf8>().toDartString();
    } finally {
      bindings.se_free(out);
    }
  }

  R withString<R>(String value, R Function(Pointer<Char>) body) {
    final p = value.toNativeUtf8();
    try {
      return body(p.cast());
    } finally {
      calloc.free(p);
    }
  }

  Pointer<se_translator> translator(Object? id) =>
      translators[id] ?? (throw _EngineError('invalid_argument', 'translator is not open'));

  commands.listen((message) {
    final m = message as List<Object?>;
    final id = m[0] as int;
    try {
      final Object? result = switch (m[1]) {
        'open' => () {
            final pointer = withString(m[2] as String,
                (spec) => native((e) => bindings.se_translator_open(spec, e), (Pointer<se_translator> p) => p == nullptr));
            translators[nextTranslator] = pointer;
            return nextTranslator++;
          }(),
        'translate' => withString(m[3] as String, (lines) => takeString((e) => bindings.se_translate(translator(m[2]), lines, e))),
        'close' => () {
            final pointer = translators.remove(m[2]);
            if (pointer != null) bindings.se_translator_close(pointer);
            return null;
          }(),
        'tracks' => withString(m[2] as String, (path) => takeString((e) => bindings.se_mkv_text_tracks(path, e))),
        'extract' => withString(m[2] as String, (path) => takeString((e) => bindings.se_mkv_extract(path, m[3] as int, e))),
        'zstd' => withString(m[2] as String, (inPath) => withString(m[3] as String, (outPath) {
              native((e) => bindings.se_zstd_decompress_file(inPath, outPath, m[4] as int, e), (int ok) => ok == 0);
              return null;
            })),
        'dispose' => () {
            translators.values.forEach(bindings.se_translator_close);
            translators.clear();
            return null;
          }(),
        _ => throw _EngineError('invalid_argument', 'unknown command'),
      };
      replies.send([id, true, result]);
    } on _EngineError catch (e) {
      replies.send([id, false, e.code, e.message]);
    }
  });
}
