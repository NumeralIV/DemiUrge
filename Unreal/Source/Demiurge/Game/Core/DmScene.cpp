// DEMIURGE — Game/Core/DmScene.cpp : see DmScene.h. Owner: C0 bridge.

#include "Game/Core/DmScene.h"
#include "GameFramework/Actor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmScene, Log, All);

namespace
{
	TArray<FDmScenePart>& Registry()
	{
		static TArray<FDmScenePart> Parts;
		return Parts;
	}

	TArray<FString> ListArg(const TCHAR* Key)
	{
		TArray<FString> Out;
		FString V;
		if (FParse::Value(FCommandLine::Get(), Key, V, false))
		{
			V.ParseIntoArray(Out, TEXT(","), true);
			for (FString& S : Out) S.TrimStartAndEndInline();
		}
		return Out;
	}
}

void FDmScene::Register(const FDmScenePart& Part)
{
	Registry().Add(Part);
}

TArray<FDmScenePart> FDmScene::Winners()
{
	TMap<FName, FDmScenePart> Best;
	for (const FDmScenePart& P : Registry())
	{
		FDmScenePart* Cur = Best.Find(P.Name);
		if (!Cur || P.Priority >= Cur->Priority) Best.Add(P.Name, P);
	}
	TArray<FDmScenePart> Out;
	Best.GenerateValueArray(Out);
	Out.Sort([](const FDmScenePart& A, const FDmScenePart& B) { return A.Name.LexicalLess(B.Name); });
	return Out;
}

TArray<AActor*> FDmScene::Build(const FDmSceneContext& Ctx, uint32 KindMask)
{
	TArray<AActor*> Out;
	const TArray<FString> Only = ListArg(TEXT("-parts="));
	const TArray<FString> Not = ListArg(TEXT("-noparts="));
	for (const FDmScenePart& P : Winners())
	{
		if ((uint32(P.Kind) & KindMask) == 0) continue;
		const FString N = P.Name.ToString();
		if (Only.Num() > 0 && !Only.Contains(N)) continue;
		if (Not.Contains(N)) continue;
		AActor* A = P.Factory ? P.Factory(Ctx) : nullptr;
		UE_LOG(LogDmScene, Display, TEXT("DmScene: part %s (priority %d%s%s) -> %s"), *N, P.Priority,
			P.Origin.IsEmpty() ? TEXT("") : TEXT(", "), *P.Origin, A ? *A->GetName() : TEXT("no actor"));
		if (A) Out.Add(A);
	}
	return Out;
}

AActor* FDmScene::BuildPart(const FDmSceneContext& Ctx, FName Name)
{
	for (const FDmScenePart& P : Winners())
		if (P.Name == Name && P.Factory) return P.Factory(Ctx);
	return nullptr;
}

bool FDmScene::Has(FName Name)
{
	for (const FDmScenePart& P : Registry()) if (P.Name == Name) return true;
	return false;
}

FString FDmScene::Describe()
{
	FString S;
	for (const FDmScenePart& P : Registry())
	{
		if (!S.IsEmpty()) S += TEXT(" ");
		S += FString::Printf(TEXT("%s(%d)"), *P.Name.ToString(), P.Priority);
	}
	return S;
}

FDmScenePartRegistrar::FDmScenePartRegistrar(const TCHAR* Name, EDmPartKind Kind, int32 Priority, FDmPartFactory Factory, const TCHAR* Origin)
{
	FDmScenePart P;
	P.Name = FName(Name);
	P.Kind = Kind;
	P.Priority = Priority;
	P.Factory = MoveTemp(Factory);
	P.Origin = Origin;
	FDmScene::Register(P);
}
