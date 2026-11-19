// DEMIURGE — Sim/Balance.h : balance overrides of the tuning structs (SPEC-SIM 3.13, balance.js).
// Owner: sim engineer (A14, key "sim") until integration; afterwards the ONLY file pair the
// balance owner (simulation integrator) edits to retune. Module authors do not retune afterwards.
// Header written by foundation-api.
//
// Tuning lives per world in w.tune<XTune>() (never saved). Overrides are applied to a world when it
// is created and again when it is loaded (sim::create / sim::use call balance::apply):
//   void balance::apply(World& w) {
//       auto& e = w.tune<EconomyTune>();  e.growthR = 0.032;   // balance overrides
//   }
// Empty at first. Overrides must be deterministic (constants, or functions of the world's seed).
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "State.h"

namespace dm::balance {

// Apply every balance override to the tuning structs of w. Idempotent (calling it twice gives the
// same tuning). Empty at first.
void apply(World& w);
// Set one tuning value by name, "<module>.<field>" (e.g. "economy.growthR"), for simrun
// experiments (--set economy.growthR=0.032). False for an unknown key or a non-finite value. The
// balance owner registers the keys it supports.
bool set(World& w, std::string_view key, double value);
// The keys set() accepts, sorted.
std::vector<std::string> keys();

} // namespace dm::balance
