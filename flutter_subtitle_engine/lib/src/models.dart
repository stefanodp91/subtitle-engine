/// The files of one translation direction, as Firefox ships them; models
/// with separate vocabularies give [vocab] (source) and [targetVocab].
final class TranslatorModelFiles {
  const TranslatorModelFiles({required this.model, required this.vocab, required this.lex, this.targetVocab});

  final String model;
  final String vocab;
  final String lex;
  final String? targetVocab;

  Map<String, String> toJson() => {
        'model': model,
        'vocab': vocab,
        'lex': lex,
        if (targetVocab != null) 'target_vocab': targetVocab!,
      };
}

/// An open translator, owned by the engine's isolate.
final class TranslatorHandle {
  const TranslatorHandle(this.id);

  final int id;
}

/// A text subtitle track inside a Matroska file.
class MkvTextTrack {
  const MkvTextTrack({
    required this.number,
    required this.codec,
    required this.language,
    required this.languageBcp47,
    required this.name,
    required this.isDefault,
    required this.isForced,
    required this.hearingImpaired,
  });

  factory MkvTextTrack.fromJson(Map<String, dynamic> json) => MkvTextTrack(
        number: json['number'] as int,
        codec: json['codec'] as String,
        language: _orNull(json['language'] as String?),
        languageBcp47: _orNull(json['language_bcp47'] as String?),
        name: _orNull(json['name'] as String?),
        isDefault: json['default'] as bool,
        isForced: json['forced'] as bool,
        hearingImpaired: json['hearing_impaired'] as bool,
      );

  final int number;

  /// `S_TEXT/UTF8`, `S_TEXT/ASS`, `S_TEXT/SSA` or `S_TEXT/WEBVTT`.
  final String codec;
  final String? language;
  final String? languageBcp47;
  final String? name;
  final bool isDefault;
  final bool isForced;
  final bool hearingImpaired;
}

/// A subtitle track of any kind, in file order (the order players number
/// their subtitle tracks in); [isText] is false for image subtitles.
final class MkvSubtitleTrack extends MkvTextTrack {
  const MkvSubtitleTrack({
    required super.number,
    required super.codec,
    required super.language,
    required super.languageBcp47,
    required super.name,
    required super.isDefault,
    required super.isForced,
    required super.hearingImpaired,
    required this.isText,
  });

  factory MkvSubtitleTrack.fromJson(Map<String, dynamic> json) {
    final t = MkvTextTrack.fromJson(json);
    return MkvSubtitleTrack(
      number: t.number,
      codec: t.codec,
      language: t.language,
      languageBcp47: t.languageBcp47,
      name: t.name,
      isDefault: t.isDefault,
      isForced: t.isForced,
      hearingImpaired: t.hearingImpaired,
      isText: json['text'] as bool,
    );
  }

  final bool isText;
}

/// One subtitle cue; rows separated by `\n`, ASS override tags kept.
final class SubtitleCue {
  const SubtitleCue({required this.start, required this.end, required this.text});

  factory SubtitleCue.fromJson(Map<String, dynamic> json) => SubtitleCue(
        start: Duration(milliseconds: json['start_ms'] as int),
        end: Duration(milliseconds: json['end_ms'] as int),
        text: json['text'] as String,
      );

  final Duration start;
  final Duration end;
  final String text;
}

/// The cues of one track; [complete] is false for a file cut short.
final class MkvExtraction {
  const MkvExtraction({required this.cues, required this.complete});

  final List<SubtitleCue> cues;
  final bool complete;
}

/// An error from the engine: [code] is one of the C interface's codes
/// (invalid_argument, model_load_failed, translation_failed, io_error,
/// not_matroska, track_not_found, not_text_track, corrupt_data, too_large).
final class SubtitleEngineException implements Exception {
  const SubtitleEngineException(this.code, this.message);

  final String code;
  final String message;

  @override
  String toString() => 'SubtitleEngineException($code: $message)';
}

String? _orNull(String? value) => value == null || value.isEmpty ? null : value;
