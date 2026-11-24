// DEMIURGE — Game/Towns/DmTownsPlan.h : the settlement planner (C5 towns). Pure data in, pure data out.
//
// PlanTown lays out one settlement on its piece of the planet, deterministically from the settlement id:
//   - a local tangent frame at the centre cell (x east, y north, metres; gnomonic, so streets are great circles);
//   - the terrain under it from the ground snapshot (a height / land / slope grid);
//   - streets: spokes toward the neighbouring settlements and the harbour that follow the contours, organic
//     ring streets at fixed radii (so a town grows into the rings it will have as a city), lanes branching
//     off them that stop where they meet another street, planned grid districts and a ring highway from
//     the Industry age;
//   - a plaza with the civic buildings of DM.BUILDINGS (shrine, temple, market, academy, wonder), granary,
//     mine, factories, power plant, launch site; walls with towers and gatehouses where the spokes cross;
//     docks with piers, moored ships, warehouses, cranes and a lighthouse on the coast;
//   - houses along both sides of every street, facing it, densest at the centre, by age (tents and huts,
//     longhouses and mud houses, cottages, townhouses, terraces, apartment slabs, villas, glass towers),
//     culture palette and climate; ruins when the settlement is dead;
//   - fields around villages (rotated strip grids per sector between the roads, sized by the age, crops
//     by climate, centre-pivot circles in dry lands from the Atomic age), hedgerows, farmsteads, windmills;
//   - the ground overlay mesh (fields, streets with night lamps, plazas, quays) draped on the terrain.
// Thread-safe: runs on worker threads against immutable snapshots.
#pragma once

#include "CoreMinimal.h"
#include <atomic>
#include <memory>
#include "Game/Towns/DmTownsKit.h"
#include "Game/Towns/DmTownsGround.h"

namespace dm { struct World; class Geo; }

/** Immutable copy of the world fields the planner reads (one per rebuild wave, shared by every task). */
struct FDmTownsWorldSnap
{
	std::shared_ptr<const dm::Geo> Geo;
	int32 N = 0;
	TArray<float> Water, Farm, Urban, Temp, Moist, Slope, Elev;
	TArray<uint8> Biome;
	TArray<int16> Owner;
	double SeaLevel = 0, Year = 0;
	/** Nearest cell to a planet-frame direction (thread-safe), -1 if none. */
	int32 CellAt(const FVector& Dir, int32 Hint = -1) const;
};

/** Footprints of every kit mesh (plain data copied from the factory for the worker threads). */
struct FDmKitTable
{
	TArray<FDmKitInfo> Infos[int32(EDmTownMesh::Num)];
	/** (wave 4 towns) per age: the real assets' footprints (empty row: the generated kit's); UDmTownsFactory::FillKitTable */
	TArray<FDmKitInfo> Aged[7][int32(EDmTownMesh::Num)];
	const FDmKitInfo& Get(EDmTownMesh Id, int32 V) const;
	/** The footprint the planner places at an age: the real asset's where one exists, else the stand-in's. */
	const FDmKitInfo& GetAt(int32 Age, EDmTownMesh Id, int32 V) const;
};

struct FDmTownInput
{
	int32 Id = -1, Cell = -1;
	FVector Dir = FVector::ZeroVector;    // planet frame, unit
	int32 Tier = 0, Age = 0, Culture = -1;
	double Pop = 0;
	FLinearColor Color = FLinearColor::White;
	uint8 B[16] = {};                     // dm::Building flags
	bool bCapital = false;
	double Ark = 0;
	bool bDead = false;
	float Ruin = 0;                       // 0..1 how much of a dead town still stands (urban at the centre)
	float Plague = 0;
	TArray<FVector> Nbrs;                 // directions of neighbouring live settlements (nearest first)
	TArray<uint8> NbrRoad;                // 1 when a road runs toward that neighbour
	uint32 Seed = 1;
	float Scale = 1.f;                    // -townscale debug multiplier of radii
	bool bFieldOverlay = true;            // draw the field quads (false: the planet paints the farmland; farmsteads stay)
	uint8 CultureStyle = 0;               // (wave 4 towns) EDmCultureStyle of the settlement's culture (material instances)
};

struct FDmTownInst
{
	EDmTownMesh Mesh = EDmTownMesh::Cottage;
	uint8 Var = 0;
	FVector3f Pos = FVector3f::ZeroVector;   // cm, planet axes, relative to the plan's centre point
	FQuat4f Rot = FQuat4f::Identity;
	FVector3f Scale = FVector3f::OneVector;
	float Cd[UDmTownsFactory::NumCustomData] = { 1, 1, 1, 1, 1, 1, 0.4f, 0 };
	uint32 Key = 0;                          // stable identity of the slot (construction diffs)
	float W = 8.f, D = 6.f, H = 6.f;         // world footprint (m) after scale
	uint8 Sub = 0;                           // (wave 4 towns) which real-asset alternative (FDmKitInfo::NumAlt; hashed from Key)
	uint8 State = 0;                         // (wave 4 towns) EDmTownState: 2 = the asset's own ruin (dead towns)
};

/** (towns-perf) The instances of one (mesh, variant), ready for an instanced component: built by the planning task. */
struct FDmTownBatch
{
	uint32 Key = 0;                          // EDmTownMesh << 8 | variant | Sub << 16 | State << 20 (DmTownsBatchKey)
	TArray<FTransform> Xf;                   // relative to the plan's centre
	TArray<float> Cd;                        // NumCustomData floats per instance
	TArray<int32> Idx;                       // index into FDmTownPlan::Inst per instance
};

struct FDmTownPlan
{
	int32 Id = -1;
	FVector Centre = FVector::ZeroVector;    // planet frame (uu), the component origin
	TArray<FDmTownInst> Inst;
	FDmTownMesh Ground;                      // overlay, metres, planet axes, relative to Centre (section 0 only)
	TArray<FVector> Smoke;                   // chimney / stack / campfire anchors (planet frame, uu)
	float RadiusM = 0.f, ExtentM = 0.f;
	int32 Houses = 0, Civic = 0, Fields = 0, Streets = 0, Piers = 0, Ships = 0, WallPieces = 0;
	double Ms = 0.0;
	uint32 Sig = 0;
	FString Note;
	// (wave 4 towns) what picks the real assets and their material instances (UDmTownsFactory / FDmTownsAssets)
	int8 Age = 0, Tier = 0, CultureStyle = 0, Climate = 0;
	int32 Slices = 0;                        // time slices the plan took (FDmTownPlanStepper)
	double MaxSliceMs = 0.0;                 // wall time (a preempted worker on a loaded machine stretches it)
	double MaxSliceCpuMs = 0.0;              // (wave 4 towns) thread CPU time of the longest slice
	// (towns-perf) prepared off the game thread by the planning task (ADmTowns): the game thread only copies these into
	// components; GroundData is consumed by the first apply (a later rebuild of the same plan falls back to Ground)
	bool bPrepared = false;
	TArray<FDmTownBatch> Batches;
	TSharedPtr<struct FDmTownsMeshData, ESPMode::ThreadSafe> GroundData;
	TArray<uint32> NewKeys;                  // instance keys of buildings that are new (scaffolds), against the plan it replaces
	double PrepMs = 0.0;
};

/** (wave 4 towns) The batch key of a component: kit id, variant, real-asset alternative and state. */
namespace DmTownsBatchKey
{
	inline uint32 Make(EDmTownMesh M, int32 V, int32 Sub, int32 State) { return (uint32(M) << 8) | uint32(V & 0xff) | (uint32(Sub & 0xf) << 16) | (uint32(State & 0x3) << 20); }
	inline EDmTownMesh Mesh(uint32 K) { return EDmTownMesh((K >> 8) & 0xff); }
	inline int32 Var(uint32 K) { return int32(K & 0xff); }
	inline int32 Sub(uint32 K) { return int32((K >> 16) & 0xf); }
	inline int32 State(uint32 K) { return int32((K >> 20) & 0x3); }
}

/** (wave 4 towns) A settlement plan made in time slices: each Step does at most about BudgetMs of work (a phase or a band
 *  of the height grid) and returns, so a planning task can hand its worker back to the scheduler between slices (the
 *  sky's weather builds and the planet's patches waited behind whole 1-7 s plans). Cancel is polled between slices and
 *  inside the long loops: an abandoned plan stops within a slice. Inputs must outlive the stepper. */
class DEMIURGE_API FDmTownPlanStepper
{
public:
	FDmTownPlanStepper(const FDmTownInput& In, const FDmTownsWorldSnap& Snap, const FDmTownsGround& Ground, const FDmKitTable& Kit, FDmTownPlan& Out,
		const std::atomic<bool>* Cancel = nullptr);
	~FDmTownPlanStepper();
	/** One slice; true when the plan is complete (or cancelled: then Out is empty). */
	bool Step(double BudgetMs);
	bool WasCancelled() const;
	struct FImpl;
private:
	TUniquePtr<FImpl> Impl;
};

namespace DmTownsPlan
{
	/** Plan one settlement. Ground and snapshot must outlive the call. */
	DEMIURGE_API void PlanTown(const FDmTownInput& In, const FDmTownsWorldSnap& Snap, const FDmTownsGround& Ground,
		const FDmKitTable& Kit, FDmTownPlan& Out);
	/** Radius (m) of the built area by tier, population and age (also the camera framing of scenarios). */
	DEMIURGE_API float BuiltRadius(int32 Tier, double Pop, int32 Age);
	/** Local tangent frame of a settlement (east, north, up) at a planet direction. */
	DEMIURGE_API void Frame(const FVector& Dir, FVector& E, FVector& N);
}
