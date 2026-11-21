#include "model_store_fixture.hpp"

TEST(ModelStore, MissingWithoutFolder) {
  ModelStore store(freshDir("missing"));
  EXPECT_EQ(store.check(enit(), CheckMode::kQuick), ModelStatus::kMissing);
  EXPECT_EQ(store.check(enit(), CheckMode::kFull), ModelStatus::kMissing);
}

TEST(ModelStore, InterruptedInstallIsIncomplete) {
  const std::string dir = freshDir("interrupted");
  ModelStore store(dir);
  writeOtherWriterLayout(dir, enit());
  fs::remove(dir + "/enit/installed.json");
  fs::create_directories(dir + "/enit/.download-se");
  EXPECT_EQ(store.check(enit(), CheckMode::kQuick), ModelStatus::kIncomplete);
  EXPECT_EQ(store.check(enit(), CheckMode::kFull), ModelStatus::kIncomplete);
}

TEST(ModelStore, OutdatedOnOtherVersion) {
  const std::string dir = freshDir("outdated");
  writeOtherWriterLayout(dir, enit("2.9"));
  EXPECT_EQ(ModelStore(dir).check(enit("3.0"), CheckMode::kQuick), ModelStatus::kOutdated);
}

TEST(ModelStore, QuickCatchesWrongSize) {
  const std::string dir = freshDir("size");
  writeOtherWriterLayout(dir, enit());
  writeAll(dir + "/enit/vocab.enit.spm", "short");
  EXPECT_EQ(ModelStore(dir).check(enit(), CheckMode::kQuick), ModelStatus::kCorrupt);
}

TEST(ModelStore, MissingFileWithMarkerIsCorrupt) {
  const std::string dir = freshDir("lost");
  writeOtherWriterLayout(dir, enit());
  fs::remove(dir + "/enit/lex.enit.bin");
  EXPECT_EQ(ModelStore(dir).check(enit(), CheckMode::kQuick), ModelStatus::kCorrupt);
}

TEST(ModelStore, FullCatchesFlippedByte) {
  const std::string dir = freshDir("flipped");
  writeOtherWriterLayout(dir, enit());
  std::string flipped = kWeights;
  flipped[100] = 'x';
  writeAll(dir + "/enit/model.enit.bin", flipped);
  ModelStore store(dir);
  EXPECT_EQ(store.check(enit(), CheckMode::kQuick), ModelStatus::kInstalled);
  EXPECT_EQ(store.check(enit(), CheckMode::kFull), ModelStatus::kCorrupt);
}

TEST(ModelStore, DownloadsPointIntoStaging) {
  const std::string dir = freshDir("downloads");
  ModelEntry m = enit();
  auto targets = ModelStore(dir).downloads(m);
  ASSERT_EQ(targets.size(), 3u);
  EXPECT_EQ(targets[0].path, dir + "/enit/.download-se/model.enit.bin.zst");
  EXPECT_EQ(targets[1].path, dir + "/enit/.download-se/vocab.enit.spm");
  EXPECT_EQ(targets[0].url, "https://example.org/model.enit.bin");
  EXPECT_EQ(targets[0].size, m.files[0].size);
  EXPECT_TRUE(fs::is_directory(dir + "/enit/.download-se"));
}

TEST(ModelStore, InstallsZstdAndPlain) {
  const std::string dir = freshDir("install");
  ModelStore store(dir);
  stage(store, enit());
  EXPECT_EQ(store.install(enit()), "");
  EXPECT_EQ(store.check(enit(), CheckMode::kFull), ModelStatus::kInstalled);
  EXPECT_EQ(readAll(dir + "/enit/model.enit.bin"), kWeights);
  EXPECT_FALSE(fs::exists(dir + "/enit/.download-se"));
}

TEST(ModelStore, InstallRejectsBadDownload) {
  const std::string dir = freshDir("bad_download");
  ModelStore store(dir);
  stage(store, enit());
  writeAll(store.downloads(enit())[1].path, "not the vocabulary");
  EXPECT_EQ(store.install(enit()), "download_corrupt");

  stage(store, enit());
  fs::remove(store.downloads(enit())[2].path);
  EXPECT_EQ(store.install(enit()), "download_missing");
}

TEST(ModelStore, InstallRejectsWrongDecompressed) {
  const std::string dir = freshDir("bad_plain");
  ModelStore store(dir);
  ModelEntry m = enit();
  m.files[0].installedSha256 = std::string(64, '0');
  stage(store, m);
  EXPECT_EQ(store.install(m), "installed_corrupt");
}

TEST(ModelStore, InstallFailureLeavesNoHalfModel) {
  const std::string dir = freshDir("no_half");
  ModelStore store(dir);
  writeOtherWriterLayout(dir, enit("2.9"));
  stage(store, enit());
  writeAll(store.downloads(enit())[0].path, "broken");
  EXPECT_NE(store.install(enit()), "");
  EXPECT_FALSE(fs::exists(dir + "/enit"));
  EXPECT_EQ(store.check(enit(), CheckMode::kQuick), ModelStatus::kMissing);
}

TEST(ModelStore, InstallReplacesOutdated) {
  const std::string dir = freshDir("replace");
  writeOtherWriterLayout(dir, enit("2.9"));
  writeAll(dir + "/enit/model.enit.old.bin", "weights of the old version");
  ModelStore store(dir);
  stage(store, enit());
  EXPECT_EQ(store.install(enit()), "");
  EXPECT_EQ(store.check(enit(), CheckMode::kFull), ModelStatus::kInstalled);
  EXPECT_FALSE(fs::exists(dir + "/enit/model.enit.old.bin"));
}

TEST(ModelStore, BusyWhileLocked) {
  const std::string dir = freshDir("busy");
  ModelStore store(dir);
  stage(store, enit());
  int fd = open((dir + "/enit.lock").c_str(), O_CREAT | O_RDWR, 0644);
  ASSERT_GE(fd, 0);
  ASSERT_EQ(flock(fd, LOCK_EX | LOCK_NB), 0);
  EXPECT_EQ(store.install(enit()), "busy");
  EXPECT_TRUE(fs::exists(store.downloads(enit())[0].path));
  EXPECT_FALSE(fs::exists(dir + "/enit/installed.json"));
  close(fd);
  EXPECT_EQ(store.install(enit()), "");
}

TEST(ModelStore, ReadsLayoutOfAnotherWriter) {
  const std::string dir = freshDir("other_writer");
  writeOtherWriterLayout(dir, enit());
  ModelStore store(dir);
  EXPECT_EQ(store.check(enit(), CheckMode::kQuick), ModelStatus::kInstalled);
  EXPECT_EQ(store.check(enit(), CheckMode::kFull), ModelStatus::kInstalled);
}

TEST(ModelStore, MarkerHoldsOnlyVersion) {
  const std::string dir = freshDir("marker");
  ModelStore store(dir);
  stage(store, enit());
  ASSERT_EQ(store.install(enit()), "");
  json marker = json::parse(readAll(dir + "/enit/installed.json"));
  EXPECT_EQ(marker, (json{{"version", "3.0"}}));
}
