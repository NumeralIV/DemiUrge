// DEMIURGE — Sim/Characters.h : the figure registry: messengers and candidates, rulers, healers, the tested,
// rival voices' figures, ordinary Lives (SPEC-MYTHOS 3.8, 3.16, 3.18, 3.21, 3.23, 4).
// Owner: characters track (M4 / M4b / M4c, key "characters"): Sim/Characters.cpp, SimTests/test_characters*.cpp,
// SimTests/stubs/Characters_stub.cpp. Header written and FROZEN by the contracts track (M0, 2026-11-20).
//
// Owns: mythos.figures (Faiths creates rival voices' figures through characters::spawn; other modules move a
// figure only through adjustCredibility / fell). Names come from Names with the culture's language (never empty).
// Generated names have no known gender: no gendered words in anything about a figure. [160 figures]
// Events: figure, trial. Phase 0: everything neutral.
#pragma once

#include <array>
#include <vector>

#include "State.h"

namespace dm {

struct CharactersTune {
	// ---- messengers (3.8)
	int candidates = 3; double askYears = 250, askDevotion = 0.45, askCost = 60;
	double credMiracle = 0.05, credFulfilled = 0.3, credBroken = -0.2, credStruck = -0.1, credDrift = 0.01;
	double criminalRedeem = 0.4, criminalRelapse = 0.2, criminalWindow = 30, childCredYear = 0.01, childYears = 80;
	double queenObey = 0.1, queenPride = 0.25, unbelievedStart = 0.4, vindicated = 0.8, shepherdTravel = 7;
	// (M0) the archetype table of 3.8: index = MessengerArchetype (None unused)
	std::array<double, 6> archCred{ 0, 0.5, 0.2, 0.35, 0.7, 0.1 };
	std::array<std::array<double, 4>, 6> archReach{ {     // rural, town, court, outcast
		{ 0, 0, 0, 0 }, { 0.9, 0.6, 0.3, 0.5 }, { 0.4, 0.5, 0.1, 1.0 }, { 0.5, 0.5, 0.2, 0.5 }, { 0.4, 0.7, 1.0, 0.1 },
		{ 0.6, 0.6, 0.6, 0.6 } } };
	double prophetYears = 60, redeemWindow = 20, msgCredMul = 1.5, parableShepherd = 0.05, decreeShepherdYears = 20;
	double queenFidelity = 0.05, queenAuthority = 0.1, childSign = 2, unbelievedFidelity = 0.05, vindicatedYears = 1000;
	// ---- trials (3.18)
	double trialPride = 0.6, trialCred = 0.6, trialMin = 20, trialMax = 40;
	std::array<double, 4> trialCost{ 80, 60, 40, 100 };
	double exaltObey = 0.1, tyrantCorrupt = 0.2, tyrantAggr = 0.2, steadfastDev = 0.1, brokenDev = -0.1, resistObey = 0.08;
	double impossibleK = 0.5, tragicAgency = 0.05;
	// ---- rulers (3.23)
	double reignMin = 15, reignMax = 45, heirShare = 0.75;
	// ---- healers (3.16) and Lives (4)
	double healerMin = 40, healerMax = 60; int maxLives = 7;
	// ---- caps (1.2)
	int maxFigures = 160;
};

namespace characters {

// The yearly pass (messengers age, travel, credibility drift and gifts / risks; rulers and successions; trials'
// results; healers; Lives at an age change); proposes MessengerChoice ("seek a voice") and Trial cards.
void yearly(World& w);
// The three would-be messengers of a town (3.8): pure read, forked per town and decade (re-arming shows the same
// three); kind Candidate, id 0. Empty for a bad or dead settlement.
std::vector<Figure> candidates(const World& w, int sid);
// Choose candidates(w, sid)[index] as the culture's messenger: creates the Figure (kind Messenger) and raises
// the prophet through society::raiseProphet(w, sid, { figure }). The figure id, or -1.
int choose(World& w, int sid, int index);
// Add a figure built by another module (Faiths' rival voices): assigns the id (mythos.nextId), fills a missing
// name from Names. The id, or -1 (cap, bad culture).
int spawn(World& w, const Figure& proto);
// A figure's end (a ruler removed by the god, a martyr, a tragic charge): sets fate and died, emits figure.
bool fell(World& w, int figureId, FigureFate fate);
// The culture's living ruler (Bronze age+, with a capital) or messenger, or -1.
int ruler(const World& w, int cultureId);
int messenger(const World& w, int cultureId);
// A figure's credibility 0..1; 0.5 (neutral: prophet visits x (0.5 + 0.5) = x 1) for -1 or a bad id.
double credibility(const World& w, int figureId);
// Move a figure's credibility (Doctrine: heard words; Omens: fulfilled warnings; Doctrine: broken promises),
// clamped 0..1. False for a bad id.
bool adjustCredibility(World& w, int figureId, double delta);
// The figure, or nullptr.
const Figure* figure(const World& w, int id);
// Begin a trial (power `test`, 3.18). False when refused.
bool test(World& w, int figureId, TrialKind kind);
// The chance that figure passes that trial (by the hidden heart); 0 for a bad id.
double trialOdds(const World& w, int figureId, TrialKind kind);

// ---- card handlers (MessengerChoice, Trial)
bool answer(World& w, int cardId, int choice);
void lapse(World& w, int cardId);
std::vector<CardOption> options(const World& w, int cardId);

} // namespace characters
} // namespace dm
