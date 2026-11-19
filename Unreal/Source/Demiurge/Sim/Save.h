// DEMIURGE — Sim/Save.h : versioned binary save / load of the whole World, slots, settings
// (old spec: SPEC-UI 6 DM.Save; the Unreal bridge saves to slots under Saved/SaveGames through this).
// Owner: save engineer (A15, key "save"). Files: Save.h, Save.cpp, SimTests/test_save.cpp,
// SimTests/stubs/Save_stub.cpp. Header written by foundation-api; the owner may add, never rename.
//
// The world itself is (de)serialized by state::serialize / state::deserialize (FOUNDATION-CPP 4.3):
// Save WRAPS it, it does not reimplement it. A save file is a container:
//   magic "DMSV", container version, flags, meta (name, seed, year, phase, age, pop, savedAt),
//   the state::serialize blob, the snapshots (optional: Snapshot is memory-only in the State format),
//   a checksum over everything before it. Little-endian on every platform.
// Loading checks magic, version, lengths and checksum; never returns a half-loaded world. The
// caller runs sim::use on the result (balance, chronicle subscription, derived fields, caches).
// Files are written atomically (temporary file + rename) so a crash never leaves a broken slot.
// Never fails hard: errors are false / nullptr / nullopt with an optional reason in *why.
// No clock in Sim: the caller passes savedAt (e.g. Unix seconds).
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "State.h"

namespace dm::save {

inline constexpr char kMagic[4] = { 'D', 'M', 'S', 'V' };
inline constexpr uint32_t kVersion = 1;
inline constexpr const char* kExt = ".dmsave";

// Small summary kept in the container header so a title screen can list slots without loading.
struct Meta {
	std::string name;
	uint32_t seed = 0;
	double year = 0;
	Phase phase = Phase::Molten;
	int age = 0;
	double pop = 0;
	int64_t savedAt = 0;                 // supplied by the caller
	bool operator==(const Meta&) const = default;
};
struct EncodeOpts {
	bool snapshots = true;               // include the time-lapse frames
	int64_t savedAt = 0;
};
struct SlotInfo {
	std::string slot;
	Meta meta;
};
// UI settings (SPEC-UI 6 Save.settings); persisted as text "key=value" lines.
struct UserSettings {
	double master = 0.8, music = 0.7, sfx = 0.9, ambience = 0.8;
	int quality = -1;                    // -1 auto, else 0 / 1 / 2
	std::string sun = "orbit";           // "orbit" (the sun circles the planet) or "follow" (always lit from the camera side)
	double tilt = 1, flashes = 1, shake = 1;   // flashes / shake default to 0.3 under reduced motion
	double uiScale = 1;
	bool labels = true, hints = true, autoSlow = true;
	bool operator==(const UserSettings&) const = default;
};

// The meta a save of w would carry now.
Meta metaOf(const World& w, int64_t savedAt = 0);
// Whole world -> container bytes. False (out empty, why set) if state::serialize refuses, or if the
// bytes would not decode again: a save that cannot be loaded is never produced (costs one load).
bool encode(const World& w, std::vector<uint8_t>& out, const EncodeOpts& opts = {}, std::string* why = nullptr);
// Container bytes -> world (with snapshots if present). nullptr on wrong magic / version, bad
// checksum, truncation, trailing bytes or a state blob deserialize rejects.
std::unique_ptr<World> decode(const uint8_t* data, size_t size, std::string* why = nullptr);
// Meta of container bytes without loading the world (nullopt if not a valid header).
std::optional<Meta> peek(const uint8_t* data, size_t size);

// Files (atomic write). Paths are UTF-8.
bool writeFile(const World& w, const std::string& path, const EncodeOpts& opts = {}, std::string* why = nullptr);
std::unique_ptr<World> readFile(const std::string& path, std::string* why = nullptr);
std::optional<Meta> readMeta(const std::string& path);

// Slots: "<dir>/<slot>.dmsave"; slot names are [A-Za-z0-9_-]{1,64} (others are refused).
std::string slotPath(const std::string& dir, std::string_view slot);
bool writeSlot(const World& w, const std::string& dir, std::string_view slot, const EncodeOpts& opts = {}, std::string* why = nullptr);
std::unique_ptr<World> readSlot(const std::string& dir, std::string_view slot, std::string* why = nullptr);
// Every readable slot in dir, sorted by slot name (unreadable files are skipped).
std::vector<SlotInfo> listSlots(const std::string& dir);
bool removeSlot(const std::string& dir, std::string_view slot);

// Settings: defaults (reduced motion lowers flashes / shake to 0.3); load merges the file over the
// defaults and ignores unknown keys and garbage values; save writes atomically. flashes and shake
// are written only when they are not 1, so a player who never changed them still gets the reduced-
// motion default on a later load (a loaded flashes = 1 can therefore not be told from "unset").
UserSettings defaultSettings(bool reducedMotion = false);
UserSettings loadSettings(const std::string& path, bool reducedMotion = false);
bool saveSettings(const UserSettings& s, const std::string& path);

} // namespace dm::save
