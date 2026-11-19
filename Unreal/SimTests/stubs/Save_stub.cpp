// DEMIURGE — SimTests/stubs/Save_stub.cpp : neutral stand-in for Sim/Save.cpp (see StubHooks.h).
// Owner: save engineer (keep it matching Save.h). Written by foundation-api.
// Neutral: nothing is written or read (false / nullptr / nullopt / empty, why = "save: stub"); no
// file is touched. metaOf, slotPath and defaultSettings / loadSettings (= the defaults) answer
// honestly: they are plain data.
#include "StubHooks.h"

DM_STUB_MODULE(save);

namespace dm::save {

Meta metaOf(const World& w, int64_t savedAt) {
	DM_STUB(save_metaOf, w, savedAt);
	Meta m;
	m.name = w.name;
	m.seed = w.seed;
	m.year = w.year;
	m.phase = w.phase;
	m.age = w.age;
	m.pop = w.stats.pop;
	m.savedAt = savedAt;
	return m;
}
bool encode(const World& w, std::vector<uint8_t>& out, const EncodeOpts& opts, std::string* why) {
	DM_STUB(save_encode, w, out, opts, why);
	out.clear();
	if (why) *why = "save: stub";
	return false;
}
std::unique_ptr<World> decode(const uint8_t* data, size_t size, std::string* why) {
	DM_STUB(save_decode, data, size, why);
	if (why) *why = "save: stub";
	return nullptr;
}
std::optional<Meta> peek(const uint8_t* data, size_t size) {
	DM_STUB(save_peek, data, size);
	return std::nullopt;
}
bool writeFile(const World& w, const std::string& path, const EncodeOpts& opts, std::string* why) {
	DM_STUB(save_writeFile, w, path, opts, why);
	if (why) *why = "save: stub";
	return false;
}
std::unique_ptr<World> readFile(const std::string& path, std::string* why) {
	DM_STUB(save_readFile, path, why);
	if (why) *why = "save: stub";
	return nullptr;
}
std::optional<Meta> readMeta(const std::string& path) {
	DM_STUB(save_readMeta, path);
	return std::nullopt;
}
std::string slotPath(const std::string& dir, std::string_view slot) {
	DM_STUB(save_slotPath, dir, slot);
	return dir + "/" + std::string(slot) + kExt;
}
bool writeSlot(const World& w, const std::string& dir, std::string_view slot, const EncodeOpts& opts, std::string* why) {
	DM_STUB(save_writeSlot, w, dir, slot, opts, why);
	if (why) *why = "save: stub";
	return false;
}
std::unique_ptr<World> readSlot(const std::string& dir, std::string_view slot, std::string* why) {
	DM_STUB(save_readSlot, dir, slot, why);
	if (why) *why = "save: stub";
	return nullptr;
}
std::vector<SlotInfo> listSlots(const std::string& dir) {
	DM_STUB(save_listSlots, dir);
	return {};
}
bool removeSlot(const std::string& dir, std::string_view slot) {
	DM_STUB(save_removeSlot, dir, slot);
	return false;
}
UserSettings defaultSettings(bool reducedMotion) {
	DM_STUB(save_defaultSettings, reducedMotion);
	UserSettings s;
	if (reducedMotion) { s.flashes = 0.3; s.shake = 0.3; }
	return s;
}
UserSettings loadSettings(const std::string& path, bool reducedMotion) {
	DM_STUB(save_loadSettings, path, reducedMotion);
	return defaultSettings(reducedMotion);
}
bool saveSettings(const UserSettings& s, const std::string& path) {
	DM_STUB(save_saveSettings, s, path);
	return false;
}

} // namespace dm::save
