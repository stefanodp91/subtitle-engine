#include "subtitle_engine/mkv_text_tracks.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>

#include "ebml.hpp"
#include "mkv_internal.hpp"

namespace subtitle_engine {

namespace {

// Element IDs (Matroska specification).
constexpr uint32_t kEbml = 0x1A45DFA3, kSegment = 0x18538067, kInfo = 0x1549A966, kTimestampScale = 0x2AD7B1,
                   kTracks = 0x1654AE6B, kTrackEntry = 0xAE, kTrackNumber = 0xD7, kTrackType = 0x83,
                   kCodecId = 0x86, kLanguage = 0x22B59C, kLanguageBcp47 = 0x22B59D, kName = 0x536E,
                   kFlagDefault = 0x88, kFlagForced = 0x55AA, kFlagHearingImpaired = 0x55AB,
                   kContentEncodings = 0x6D80, kContentEncoding = 0x6240, kContentCompression = 0x5034,
                   kContentCompAlgo = 0x4254, kContentCompSettings = 0x4255, kCluster = 0x1F43B675,
                   kTimestamp = 0xE7, kSimpleBlock = 0xA3, kBlockGroup = 0xA0, kBlock = 0xA1,
                   kBlockDuration = 0x9B, kCues = 0x1C53BB6B, kTags = 0x1254C367, kChapters = 0x1043A770,
                   kAttachments = 0x1941A469, kSeekHead = 0x114D9B74;

constexpr uint64_t kSubtitleTrackType = 0x11;
// A subtitle block never needs more than this once decompressed.
constexpr size_t kMaxBlockBytes = 1 << 20;

struct TrackInfo {
  TextTrack track;
  uint64_t type = 0;
  ContentEncoding encoding;
  bool legacyWebVtt = false;  // D_WEBVTT/*: each block starts with the cue id and settings lines
};

struct FileCloser {
  void operator()(FILE* f) const { fclose(f); }
};

bool isSegmentChild(uint32_t id) {
  return id == kCluster || id == kCues || id == kTags || id == kChapters || id == kAttachments ||
         id == kSeekHead || id == kInfo || id == kTracks;
}

bool isTextCodec(const std::string& codec) {
  return codec == "S_TEXT/UTF8" || codec == "S_TEXT/ASS" || codec == "S_TEXT/SSA" || codec == "S_TEXT/WEBVTT";
}

// Calls visit(id, size, end) for each child of [start, end); stops when visit returns false.
template <class Visit>
bool forEachChild(EbmlReader& r, int64_t end, Visit visit) {
  while (r.position() < end) {
    uint32_t id;
    uint64_t size;
    bool unknown;
    if (!r.readId(&id) || !r.readSize(&size, &unknown)) return false;
    int64_t childEnd = unknown ? end : r.position() + static_cast<int64_t>(size);
    if (childEnd > r.fileSize()) return false;
    int64_t dataStart = r.position();
    if (!visit(id, size, childEnd)) return true;
    if (!r.seek(std::max(childEnd, dataStart))) return false;
  }
  return true;
}

void parseContentEncodings(EbmlReader& r, int64_t end, ContentEncoding* out) {
  forEachChild(r, end, [&](uint32_t id, uint64_t, int64_t encEnd) {
    if (id != kContentEncoding) return true;
    forEachChild(r, encEnd, [&](uint32_t cid, uint64_t, int64_t compEnd) {
      if (cid != kContentCompression) return true;
      out->kind = ContentEncoding::Kind::Zlib;  // ContentCompAlgo defaults to 0, zlib
      forEachChild(r, compEnd, [&](uint32_t pid, uint64_t psize, int64_t) {
        uint64_t algo;
        if (pid == kContentCompAlgo && r.readUInt(psize, &algo)) {
          out->kind = algo == 3 ? ContentEncoding::Kind::HeaderStripping
                      : algo == 0 ? ContentEncoding::Kind::Zlib
                                  : ContentEncoding::Kind::None;
        } else if (pid == kContentCompSettings) {
          r.readBytes(psize, &out->strippedBytes);
        }
        return true;
      });
      return true;
    });
    return true;
  });
}

TrackInfo parseTrackEntry(EbmlReader& r, int64_t end) {
  TrackInfo info;
  info.track.language = "eng";
  forEachChild(r, end, [&](uint32_t id, uint64_t size, int64_t childEnd) {
    uint64_t v;
    switch (id) {
      case kTrackNumber: if (r.readUInt(size, &v)) info.track.number = v; break;
      case kTrackType: if (r.readUInt(size, &v)) info.type = v; break;
      case kCodecId: r.readBytes(size, &info.track.codec); break;
      case kLanguage: r.readBytes(size, &info.track.language); break;
      case kLanguageBcp47: r.readBytes(size, &info.track.languageBcp47); break;
      case kName: r.readBytes(size, &info.track.name); break;
      case kFlagDefault: if (r.readUInt(size, &v)) info.track.isDefault = v != 0; break;
      case kFlagForced: if (r.readUInt(size, &v)) info.track.isForced = v != 0; break;
      case kFlagHearingImpaired: if (r.readUInt(size, &v)) info.track.hearingImpaired = v != 0; break;
      case kContentEncodings: parseContentEncodings(r, childEnd, &info.encoding); break;
      default: break;
    }
    return true;
  });
  for (std::string* s : {&info.track.codec, &info.track.language, &info.track.languageBcp47, &info.track.name}) {
    s->erase(std::find(s->begin(), s->end(), '\0'), s->end());
  }
  // ffmpeg still writes the older WebVTT mapping (D_WEBVTT/SUBTITLES, …/CAPTIONS…).
  if (info.track.codec.rfind("D_WEBVTT/", 0) == 0) {
    info.track.codec = "S_TEXT/WEBVTT";
    info.legacyWebVtt = true;
  }
  return info;
}

struct Header {
  std::unique_ptr<FILE, FileCloser> file;
  std::unique_ptr<EbmlReader> reader;
  int64_t segmentEnd = 0;
  uint64_t timestampScale = 1000000;
  std::vector<TrackInfo> tracks;
  int64_t firstClusterAt = -1;  // position of the first cluster's ID
};

// Reads the EBML header, the segment, Info and Tracks, stopping at the first cluster.
bool readHeader(const std::string& path, Header* h, std::string* errorCode) {
  h->file.reset(fopen(path.c_str(), "rb"));
  if (!h->file) {
    *errorCode = "io_error";
    return false;
  }
  h->reader = std::make_unique<EbmlReader>(h->file.get());
  EbmlReader& r = *h->reader;
  uint32_t id;
  uint64_t size;
  bool unknown;
  if (!r.readId(&id) || id != kEbml || !r.readSize(&size, &unknown) || unknown || !r.seek(r.position() + size) ||
      !r.readId(&id) || id != kSegment || !r.readSize(&size, &unknown)) {
    *errorCode = "not_matroska";
    return false;
  }
  h->segmentEnd = unknown ? r.fileSize() : std::min<int64_t>(r.position() + size, r.fileSize());
  while (r.position() < h->segmentEnd) {
    int64_t at = r.position();
    if (!r.readId(&id) || !r.readSize(&size, &unknown)) break;
    if (id == kCluster) {
      h->firstClusterAt = at;
      break;
    }
    int64_t end = unknown ? h->segmentEnd : std::min<int64_t>(r.position() + size, r.fileSize());
    if (id == kInfo) {
      forEachChild(r, end, [&](uint32_t cid, uint64_t csize, int64_t) {
        uint64_t v;
        if (cid == kTimestampScale && r.readUInt(csize, &v) && v > 0) h->timestampScale = v;
        return true;
      });
    } else if (id == kTracks) {
      forEachChild(r, end, [&](uint32_t cid, uint64_t, int64_t entryEnd) {
        if (cid == kTrackEntry) h->tracks.push_back(parseTrackEntry(r, entryEnd));
        return true;
      });
    }
    if (!r.seek(end)) break;
  }
  return true;
}

std::string textFromPayload(const TrackInfo& info, std::string payload) {
  const std::string& codec = info.track.codec;
  payload.erase(std::find(payload.begin(), payload.end(), '\0'), payload.end());
  if (info.legacyWebVtt) {
    for (int line = 0; line < 2; ++line) {
      size_t nl = payload.find('\n');
      payload = nl == std::string::npos ? std::string() : payload.substr(nl + 1);
    }
  }
  if (codec == "S_TEXT/ASS" || codec == "S_TEXT/SSA") {
    // ReadOrder, Layer, Style, Name, MarginL, MarginR, MarginV, Effect, Text
    size_t at = 0;
    for (int commas = 0; commas < 8 && at != std::string::npos; ++commas) {
      at = payload.find(',', at);
      if (at != std::string::npos) ++at;
    }
    if (at != std::string::npos) payload = payload.substr(at);
    for (const char* br : {"\\N", "\\n"}) {
      for (size_t p = payload.find(br); p != std::string::npos; p = payload.find(br, p + 1)) payload.replace(p, 2, "\n");
    }
  }
  for (size_t p = payload.find("\r\n"); p != std::string::npos; p = payload.find("\r\n", p)) payload.erase(p, 1);
  while (!payload.empty() && (payload.back() == '\n' || payload.back() == '\r')) payload.pop_back();
  return payload;
}

// One Block or SimpleBlock body; appends a cue when it belongs to the track.
void readBlock(EbmlReader& r, int64_t end, uint64_t track, int64_t clusterTs, int64_t durationTs,
               const TrackInfo& info, uint64_t scale, std::vector<Cue>* cues) {
  uint64_t number;
  bool unknown;
  if (!r.readSize(&number, &unknown) || number != track) return;
  std::string head;
  if (r.position() + 3 > end || !r.readBytes(3, &head)) return;
  int16_t relative = static_cast<int16_t>((static_cast<uint8_t>(head[0]) << 8) | static_cast<uint8_t>(head[1]));
  if (static_cast<uint8_t>(head[2]) & 0x06) return;  // laced blocks: never used for text subtitles
  std::string payload;
  if (!r.readBytes(end - r.position(), &payload)) return;
  auto decoded = decodeBlockPayload(payload, info.encoding);
  if (!decoded) return;
  int64_t start = (clusterTs + relative) * static_cast<int64_t>(scale) / 1000000;
  int64_t length = durationTs * static_cast<int64_t>(scale) / 1000000;
  cues->push_back({start, start + length, textFromPayload(info, *decoded)});
}

}  // namespace

std::optional<std::string> decodeBlockPayload(const std::string& payload, const ContentEncoding& encoding) {
  switch (encoding.kind) {
    case ContentEncoding::Kind::None: return payload;
    case ContentEncoding::Kind::HeaderStripping: return encoding.strippedBytes + payload;
    case ContentEncoding::Kind::Zlib: break;
  }
  z_stream stream{};
  if (inflateInit(&stream) != Z_OK) return std::nullopt;
  stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(payload.data()));
  stream.avail_in = static_cast<uInt>(payload.size());
  std::string out;
  char buffer[4096];
  int status;
  do {
    stream.next_out = reinterpret_cast<Bytef*>(buffer);
    stream.avail_out = sizeof(buffer);
    status = inflate(&stream, Z_NO_FLUSH);
    if (status != Z_OK && status != Z_STREAM_END) {
      inflateEnd(&stream);
      return std::nullopt;
    }
    out.append(buffer, sizeof(buffer) - stream.avail_out);
    if (out.size() > kMaxBlockBytes) {
      inflateEnd(&stream);
      return std::nullopt;
    }
  } while (status != Z_STREAM_END);
  inflateEnd(&stream);
  return out;
}

std::vector<TextTrack> listTextTracks(const std::string& path, std::string* errorCode) {
  Header h;
  if (!readHeader(path, &h, errorCode)) return {};
  std::vector<TextTrack> out;
  for (const auto& info : h.tracks) {
    if (info.type == kSubtitleTrackType && isTextCodec(info.track.codec)) out.push_back(info.track);
  }
  return out;
}

std::optional<Extraction> extractTextTrack(const std::string& path, uint64_t trackNumber, std::string* errorCode) {
  Header h;
  if (!readHeader(path, &h, errorCode)) return std::nullopt;
  auto it = std::find_if(h.tracks.begin(), h.tracks.end(),
                         [&](const TrackInfo& t) { return t.track.number == trackNumber; });
  if (it == h.tracks.end()) {
    *errorCode = "track_not_found";
    return std::nullopt;
  }
  if (it->type != kSubtitleTrackType || !isTextCodec(it->track.codec)) {
    *errorCode = "not_text_track";
    return std::nullopt;
  }
  Extraction result;
  if (h.firstClusterAt < 0) return result;
  EbmlReader& r = *h.reader;
  r.seek(h.firstClusterAt);
  bool truncated = false;
  while (r.position() < h.segmentEnd) {
    uint32_t id;
    uint64_t size;
    bool unknown;
    if (!r.readId(&id) || !r.readSize(&size, &unknown)) {
      truncated = r.position() < h.segmentEnd;
      break;
    }
    int64_t end = unknown ? h.segmentEnd : r.position() + static_cast<int64_t>(size);
    if (id != kCluster) {
      if (end > r.fileSize() || !r.seek(end)) {
        truncated = end > r.fileSize();
        break;
      }
      continue;
    }
    int64_t clusterTs = 0;
    // Children of a cluster; an unknown-size cluster ends at the next segment-level element.
    while (r.position() < std::min<int64_t>(end, r.fileSize())) {
      int64_t at = r.position();
      uint32_t cid;
      uint64_t csize;
      bool cunknown;
      if (!r.readId(&cid) || !r.readSize(&csize, &cunknown) || cunknown) {
        truncated = true;
        break;
      }
      if (unknown && isSegmentChild(cid)) {
        r.seek(at);
        break;
      }
      int64_t childEnd = r.position() + static_cast<int64_t>(csize);
      if (childEnd > r.fileSize()) {
        truncated = true;
        break;
      }
      if (cid == kTimestamp) {
        uint64_t v;
        if (r.readUInt(csize, &v)) clusterTs = static_cast<int64_t>(v);
      } else if (cid == kSimpleBlock) {
        readBlock(r, childEnd, trackNumber, clusterTs, 0, *it, h.timestampScale, &result.cues);
      } else if (cid == kBlockGroup) {
        int64_t blockAt = -1, blockEnd = -1;
        int64_t duration = 0;
        forEachChild(r, childEnd, [&](uint32_t gid, uint64_t gsize, int64_t gEnd) {
          uint64_t v;
          if (gid == kBlock) {
            blockAt = r.position();
            blockEnd = gEnd;
          } else if (gid == kBlockDuration && r.readUInt(gsize, &v)) {
            duration = static_cast<int64_t>(v);
          }
          return true;
        });
        if (blockAt >= 0 && r.seek(blockAt)) {
          readBlock(r, blockEnd, trackNumber, clusterTs, duration, *it, h.timestampScale, &result.cues);
        }
      }
      if (!r.seek(childEnd)) {
        truncated = true;
        break;
      }
    }
    if (truncated) break;
    if (!unknown && !r.seek(std::min<int64_t>(end, r.fileSize()))) break;
    if (!unknown && end > r.fileSize()) {
      truncated = true;
      break;
    }
  }
  std::stable_sort(result.cues.begin(), result.cues.end(),
                   [](const Cue& a, const Cue& b) { return a.startMs < b.startMs; });
  result.complete = !truncated;
  return result;
}

}  // namespace subtitle_engine
