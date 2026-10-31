// Runs on the Mac against build/mac-release/engine-capi/libsubtitle_engine.dylib
// (override with SUBTITLE_ENGINE_LIB) and the translators of fixtures/models/
// (scripts/fetch_test_models.sh).
import 'dart:io';

import 'package:flutter_subtitle_engine/flutter_subtitle_engine.dart';
import 'package:test/test.dart';

final _root = Directory.current.parent.path;
final _library = Platform.environment['SUBTITLE_ENGINE_LIB'] ??
    '$_root/build/mac-release/engine-capi/libsubtitle_engine.dylib';

TranslatorModelFiles _pair(String pair) {
  final dir = '$_root/fixtures/models/$pair';
  return TranslatorModelFiles(
    model: '$dir/model.$pair.intgemm.alphas.bin',
    vocab: '$dir/vocab.$pair.spm',
    lex: '$dir/lex.50.50.$pair.s2t.bin',
  );
}

void main() {
  late SubtitleEngine engine;

  setUpAll(() async => engine = await SubtitleEngine.start(libraryPath: _library));
  tearDownAll(() => engine.dispose());

  test('translates a line from English to Italian', () async {
    final t = await engine.openTranslator([_pair('enit')]);
    final out = await engine.translate(t, ['Hello world, how are you?']);
    expect(out.single, contains('Ciao'));
    await engine.closeTranslator(t);
  });

  test('keeps line count and empty lines', () async {
    final t = await engine.openTranslator([_pair('enit')]);
    final out = await engine.translate(t, ['A.', '', 'B.']);
    expect(out, hasLength(3));
    expect(out[1], isEmpty);
    await engine.closeTranslator(t);
  });

  test('pivots Spanish to Italian through English', () async {
    final t = await engine.openTranslator([_pair('esen'), _pair('enit')]);
    final out = await engine.translate(t, ['¿Dónde está la estación de tren?']);
    expect(out.single, contains('stazione'));
    await engine.closeTranslator(t);
  });

  test('extracts the five cues of subs_srt.mkv', () async {
    final path = '$_root/fixtures/subs_srt.mkv';
    final tracks = await engine.mkvTextTracks(path);
    expect(tracks.single.codec, 'S_TEXT/UTF8');
    expect(tracks.single.language, 'eng');
    final extraction = await engine.extractMkvTrack(path, tracks.single.number);
    expect(extraction.complete, isTrue);
    expect(extraction.cues.map((c) => c.start.inSeconds), [2, 5, 8, 11, 14]);
    expect(extraction.cues[2].text, contains('\n'));
  });

  test('throws SubtitleEngineException with code model_load_failed for a missing model', () async {
    final missing = TranslatorModelFiles(model: '/nope/m.bin', vocab: '/nope/v.spm', lex: '/nope/l.bin');
    await expectLater(
      engine.openTranslator([missing]),
      throwsA(isA<SubtitleEngineException>().having((e) => e.code, 'code', 'model_load_failed')),
    );
  });

  test('throws SubtitleEngineException with code not_matroska for a text file', () async {
    final file = File('${Directory.systemTemp.path}/se_not_mkv.txt')..writeAsStringSync('not a video');
    await expectLater(
      engine.mkvTextTracks(file.path),
      throwsA(isA<SubtitleEngineException>().having((e) => e.code, 'code', 'not_matroska')),
    );
  });

  test('rejects a closed translator with code invalid_argument', () async {
    final t = await engine.openTranslator([_pair('enit')]);
    await engine.closeTranslator(t);
    await expectLater(
      engine.translate(t, ['Hello.']),
      throwsA(isA<SubtitleEngineException>().having((e) => e.code, 'code', 'invalid_argument')),
    );
  });

  test('runs two translate calls concurrently and returns both in order', () async {
    final t = await engine.openTranslator([_pair('enit')]);
    final results = await Future.wait([
      engine.translate(t, ['Good morning.']),
      engine.translate(t, ['Good night.']),
    ]);
    expect(results[0].single, contains('Buongiorno'));
    expect(results[1].single, contains('notte'));
    await engine.closeTranslator(t);
  });

  test('decompresses a zstd file and rejects one that is too large', () async {
    final dir = Directory.systemTemp.createTempSync('se_zstd');
    final source = File('${dir.path}/text.txt')..writeAsStringSync('A subtitle line.\n' * 10000);
    final packed = await Process.run('zstd', ['-q', '-f', source.path, '-o', '${dir.path}/text.zst']);
    expect(packed.exitCode, 0, reason: 'needs the zstd command');
    await engine.decompressZstd('${dir.path}/text.zst', '${dir.path}/out.txt', maxBytes: source.lengthSync());
    expect(File('${dir.path}/out.txt').readAsStringSync(), source.readAsStringSync());
    await expectLater(
      engine.decompressZstd('${dir.path}/text.zst', '${dir.path}/small.txt', maxBytes: 100),
      throwsA(isA<SubtitleEngineException>().having((e) => e.code, 'code', 'too_large')),
    );
    expect(File('${dir.path}/small.txt').existsSync(), isFalse);
    dir.deleteSync(recursive: true);
  });

  test('translates with a model that has separate vocabularies', () async {
    final dir = '$_root/fixtures/models/zh-Hanten';
    final t = await engine.openTranslator([
      TranslatorModelFiles(
        model: '$dir/model.zhen.intgemm.alphas.bin',
        vocab: '$dir/srcvocab.zhen.spm',
        targetVocab: '$dir/trgvocab.zhen.spm',
        lex: '$dir/lex.50.50.zhen.s2t.bin',
      ),
    ]);
    final out = await engine.translate(t, ['你好，世界。']);
    expect(out.single.toLowerCase(), contains('hello'));
    await engine.closeTranslator(t);
  });
}
