#include "ChaosClothUniversalSelectionRemapNode.h"

#include "ChaosClothAsset/ClothCollectionGroup.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/CollectionClothRenderPatternFacade.h"
#include "ChaosClothAsset/CollectionClothSelectionFacade.h"
#include "ChaosClothAsset/CollectionClothSimPatternFacade.h"
#include "Dataflow/DataflowInputOutput.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosClothUniversalSelectionRemapNode)

#define LOCTEXT_NAMESPACE "FChaosClothUniversalSelectionRemapNode"

namespace UE::Chaos::ClothAsset::UniversalSelectionRemap
{
    struct FQ2
    {
        int64 X = 0;
        int64 Y = 0;

        bool operator==(const FQ2& Other) const { return X == Other.X && Y == Other.Y; }
        bool operator<(const FQ2& Other) const { return X != Other.X ? X < Other.X : Y < Other.Y; }
    };

    FORCEINLINE uint32 GetTypeHash(const FQ2& Value)
    {
        return HashCombine(::GetTypeHash(Value.X), ::GetTypeHash(Value.Y));
    }

    struct FTriKey
    {
        FName RegionTag = NAME_None;
        FQ2 P0;
        FQ2 P1;
        FQ2 P2;

        bool operator==(const FTriKey& Other) const
        {
            return RegionTag == Other.RegionTag && P0 == Other.P0 && P1 == Other.P1 && P2 == Other.P2;
        }
    };

    static uint32 HashNameStable(const FName Name)
    {
        // Avoid relying on branch-specific GetTypeHash overloads for FName/FString.
        // A tiny FNV-1a pass is sufficient here because this is only a TMap key hash;
        // equality is still checked by FTriKey::operator==.
        const FString Text = Name.ToString();
        uint32 Hash = 2166136261u;
        for (const TCHAR Ch : Text)
        {
            Hash ^= static_cast<uint32>(Ch);
            Hash *= 16777619u;
        }
        return Hash;
    }

    FORCEINLINE uint32 GetTypeHash(const FTriKey& Value)
    {
        uint32 Hash = HashNameStable(Value.RegionTag);
        Hash = HashCombine(Hash, GetTypeHash(Value.P0));
        Hash = HashCombine(Hash, GetTypeHash(Value.P1));
        Hash = HashCombine(Hash, GetTypeHash(Value.P2));
        return Hash;
    }

    struct FMaps
    {
        TArray<int32> Face;
        TArray<int32> Vertex;
        int32 MatchedFaces = 0;
    };

    static FQ2 Quantize(const FVector2f& P, const float Tolerance)
    {
        const double Inv = 1.0 / FMath::Max<double>(Tolerance, 1.e-9);
        return { FMath::RoundToInt64(static_cast<double>(P.X) * Inv), FMath::RoundToInt64(static_cast<double>(P.Y) * Inv) };
    }

    static FTriKey MakeTriKey(FName RegionTag, const FVector2f& A, const FVector2f& B, const FVector2f& C, const float Tolerance)
    {
        FQ2 P[3] = { Quantize(A, Tolerance), Quantize(B, Tolerance), Quantize(C, Tolerance) };
        if (P[1] < P[0]) Swap(P[0], P[1]);
        if (P[2] < P[1]) Swap(P[1], P[2]);
        if (P[1] < P[0]) Swap(P[0], P[1]);
        return { RegionTag, P[0], P[1], P[2] };
    }

    static void ComputeBounds(TConstArrayView<FVector3f> Positions, FVector3f& OutMin, FVector3f& OutMax)
    {
        OutMin = FVector3f(FLT_MAX, FLT_MAX, FLT_MAX);
        OutMax = FVector3f(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        for (const FVector3f& P : Positions)
        {
            OutMin.X = FMath::Min(OutMin.X, P.X); OutMin.Y = FMath::Min(OutMin.Y, P.Y); OutMin.Z = FMath::Min(OutMin.Z, P.Z);
            OutMax.X = FMath::Max(OutMax.X, P.X); OutMax.Y = FMath::Max(OutMax.Y, P.Y); OutMax.Z = FMath::Max(OutMax.Z, P.Z);
        }
        if (Positions.IsEmpty())
        {
            OutMin = FVector3f::ZeroVector;
            OutMax = FVector3f::OneVector;
        }
    }

    static FVector3f NormalizePosition(const FVector3f& P, const FVector3f& Min, const FVector3f& Max)
    {
        FVector3f Size = Max - Min;
        Size.X = FMath::Max(Size.X, UE_SMALL_NUMBER);
        Size.Y = FMath::Max(Size.Y, UE_SMALL_NUMBER);
        Size.Z = FMath::Max(Size.Z, UE_SMALL_NUMBER);
        return FVector3f((P.X - Min.X) / Size.X, (P.Y - Min.Y) / Size.Y, (P.Z - Min.Z) / Size.Z);
    }

    static FVector3f FaceCentroid(TConstArrayView<FVector3f> Positions, const FIntVector3& Tri)
    {
        if (!Positions.IsValidIndex(Tri[0]) || !Positions.IsValidIndex(Tri[1]) || !Positions.IsValidIndex(Tri[2]))
        {
            return FVector3f::ZeroVector;
        }
        return (Positions[Tri[0]] + Positions[Tri[1]] + Positions[Tri[2]]) / 3.f;
    }

    static int32 BestCornerPermutation(
        const FVector2f SourceUV[3],
        const FVector2f TargetUV[3],
        int32 OutTargetCornerForSource[3])
    {
        static constexpr int32 Permutations[6][3] =
        {
            {0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0}
        };

        float BestScore = TNumericLimits<float>::Max();
        int32 Best = 0;
        for (int32 P = 0; P < 6; ++P)
        {
            float Score = 0.f;
            for (int32 C = 0; C < 3; ++C)
            {
                Score += FVector2f::DistSquared(SourceUV[C], TargetUV[Permutations[P][C]]);
            }
            if (Score < BestScore)
            {
                BestScore = Score;
                Best = P;
            }
        }
        for (int32 C = 0; C < 3; ++C)
        {
            OutTargetCornerForSource[C] = Permutations[Best][C];
        }
        return Best;
    }

    static TArray<FName> BuildRenderFaceTags(const FCollectionClothConstFacade& Facade)
    {
        TArray<FName> Tags;
        Tags.Init(NAME_None, Facade.GetRenderIndices().Num());
        for (int32 PatternIndex = 0; PatternIndex < Facade.GetNumRenderPatterns(); ++PatternIndex)
        {
            FCollectionClothRenderPatternConstFacade Pattern = Facade.GetRenderPattern(PatternIndex);
            const FString TagString = FString::Printf(
                TEXT("%s|F%d|V%d"),
                *Pattern.GetRenderMaterialSoftObjectPathName().ToString(),
                Pattern.GetNumRenderFaces(),
                Pattern.GetNumRenderVertices());
            const FName Tag(*TagString);
            const int32 Offset = Pattern.GetRenderFacesOffset();
            for (int32 LocalFace = 0; LocalFace < Pattern.GetNumRenderFaces(); ++LocalFace)
            {
                if (Tags.IsValidIndex(Offset + LocalFace))
                {
                    Tags[Offset + LocalFace] = Tag;
                }
            }
        }
        return Tags;
    }

    static TArray<FName> BuildSimFaceTags(const FCollectionClothConstFacade& Facade)
    {
        TArray<FName> Tags;
        Tags.Init(NAME_None, Facade.GetSimIndices2D().Num());
        for (int32 PatternIndex = 0; PatternIndex < Facade.GetNumSimPatterns(); ++PatternIndex)
        {
            FCollectionClothSimPatternConstFacade Pattern = Facade.GetSimPattern(PatternIndex);
            const FString TagString = FString::Printf(
                TEXT("Fabric%d|F%d|V%d"),
                Pattern.GetFabricIndex(),
                Pattern.GetNumSimFaces(),
                Pattern.GetNumSimVertices2D());
            const FName Tag(*TagString);
            const int32 Offset = Pattern.GetSimFacesOffset();
            for (int32 LocalFace = 0; LocalFace < Pattern.GetNumSimFaces(); ++LocalFace)
            {
                if (Tags.IsValidIndex(Offset + LocalFace))
                {
                    Tags[Offset + LocalFace] = Tag;
                }
            }
        }
        return Tags;
    }

    static bool GetRenderUV(TConstArrayView<TArray<FVector2f>> UVs, const int32 Vertex, const int32 Channel, FVector2f& OutUV)
    {
        if (!UVs.IsValidIndex(Vertex) || !UVs[Vertex].IsValidIndex(Channel))
        {
            return false;
        }
        OutUV = UVs[Vertex][Channel];
        return true;
    }

    static FMaps BuildRenderMapsForChannel(
        const FCollectionClothConstFacade& Source,
        const FCollectionClothConstFacade& Target,
        const int32 UVChannel,
        const float Tolerance,
        const bool bUseRegionTags)
    {
        FMaps Result;
        const TConstArrayView<FIntVector3> SourceFaces = Source.GetRenderIndices();
        const TConstArrayView<FIntVector3> TargetFaces = Target.GetRenderIndices();
        const TConstArrayView<FVector3f> SourcePositions = Source.GetRenderPosition();
        const TConstArrayView<FVector3f> TargetPositions = Target.GetRenderPosition();
        const TConstArrayView<TArray<FVector2f>> SourceUVs = Source.GetRenderUVs();
        const TConstArrayView<TArray<FVector2f>> TargetUVs = Target.GetRenderUVs();

        Result.Face.Init(INDEX_NONE, SourceFaces.Num());
        Result.Vertex.Init(INDEX_NONE, SourcePositions.Num());

        const TArray<FName> SourceTags = BuildRenderFaceTags(Source);
        const TArray<FName> TargetTags = BuildRenderFaceTags(Target);

        TMap<FTriKey, TArray<int32>> TargetByKey;
        for (int32 FaceIndex = 0; FaceIndex < TargetFaces.Num(); ++FaceIndex)
        {
            const FIntVector3& Tri = TargetFaces[FaceIndex];
            FVector2f UV[3];
            if (!GetRenderUV(TargetUVs, Tri[0], UVChannel, UV[0]) ||
                !GetRenderUV(TargetUVs, Tri[1], UVChannel, UV[1]) ||
                !GetRenderUV(TargetUVs, Tri[2], UVChannel, UV[2]))
            {
                continue;
            }
            const FName RegionTag = bUseRegionTags && TargetTags.IsValidIndex(FaceIndex) ? TargetTags[FaceIndex] : NAME_None;
            TargetByKey.FindOrAdd(MakeTriKey(RegionTag, UV[0], UV[1], UV[2], Tolerance)).Add(FaceIndex);
        }

        FVector3f SourceMin, SourceMax, TargetMin, TargetMax;
        ComputeBounds(SourcePositions, SourceMin, SourceMax);
        ComputeBounds(TargetPositions, TargetMin, TargetMax);

        TSet<int32> UsedTargetFaces;
        TArray<TMap<int32, int32>> VertexVotes;
        VertexVotes.SetNum(SourcePositions.Num());

        for (int32 SourceFaceIndex = 0; SourceFaceIndex < SourceFaces.Num(); ++SourceFaceIndex)
        {
            const FIntVector3& SourceTri = SourceFaces[SourceFaceIndex];
            FVector2f SourceFaceUV[3];
            if (!GetRenderUV(SourceUVs, SourceTri[0], UVChannel, SourceFaceUV[0]) ||
                !GetRenderUV(SourceUVs, SourceTri[1], UVChannel, SourceFaceUV[1]) ||
                !GetRenderUV(SourceUVs, SourceTri[2], UVChannel, SourceFaceUV[2]))
            {
                continue;
            }

            const FName RegionTag = bUseRegionTags && SourceTags.IsValidIndex(SourceFaceIndex) ? SourceTags[SourceFaceIndex] : NAME_None;
            const FTriKey Key = MakeTriKey(RegionTag,
                SourceFaceUV[0], SourceFaceUV[1], SourceFaceUV[2], Tolerance);
            const TArray<int32>* Candidates = TargetByKey.Find(Key);
            if (!Candidates || Candidates->IsEmpty())
            {
                continue;
            }

            const FVector3f SourceCentroidN = NormalizePosition(FaceCentroid(SourcePositions, SourceTri), SourceMin, SourceMax);
            int32 BestTargetFace = INDEX_NONE;
            float BestScore = TNumericLimits<float>::Max();
            for (const int32 Candidate : *Candidates)
            {
                if (UsedTargetFaces.Contains(Candidate) || !TargetFaces.IsValidIndex(Candidate))
                {
                    continue;
                }
                const FVector3f TargetCentroidN = NormalizePosition(FaceCentroid(TargetPositions, TargetFaces[Candidate]), TargetMin, TargetMax);
                const float Score = FVector3f::DistSquared(SourceCentroidN, TargetCentroidN);
                if (Score < BestScore)
                {
                    BestScore = Score;
                    BestTargetFace = Candidate;
                }
            }
            if (BestTargetFace == INDEX_NONE)
            {
                continue;
            }

            UsedTargetFaces.Add(BestTargetFace);
            Result.Face[SourceFaceIndex] = BestTargetFace;
            ++Result.MatchedFaces;

            const FIntVector3& TargetTri = TargetFaces[BestTargetFace];
            FVector2f TargetFaceUV[3];
            if (!GetRenderUV(TargetUVs, TargetTri[0], UVChannel, TargetFaceUV[0]) ||
                !GetRenderUV(TargetUVs, TargetTri[1], UVChannel, TargetFaceUV[1]) ||
                !GetRenderUV(TargetUVs, TargetTri[2], UVChannel, TargetFaceUV[2]))
            {
                continue;
            }

            int32 Perm[3];
            BestCornerPermutation(SourceFaceUV, TargetFaceUV, Perm);
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                const int32 SourceVertex = SourceTri[Corner];
                const int32 TargetVertex = TargetTri[Perm[Corner]];
                if (VertexVotes.IsValidIndex(SourceVertex))
                {
                    ++VertexVotes[SourceVertex].FindOrAdd(TargetVertex);
                }
            }
        }

        for (int32 SourceVertex = 0; SourceVertex < VertexVotes.Num(); ++SourceVertex)
        {
            int32 BestTarget = INDEX_NONE;
            int32 BestVotes = 0;
            for (const TPair<int32, int32>& Pair : VertexVotes[SourceVertex])
            {
                if (Pair.Value > BestVotes)
                {
                    BestVotes = Pair.Value;
                    BestTarget = Pair.Key;
                }
            }
            Result.Vertex[SourceVertex] = BestTarget;
        }
        return Result;
    }

    static FMaps BuildRenderMaps(
        const FCollectionClothConstFacade& Source,
        const FCollectionClothConstFacade& Target,
        const int32 RequestedChannel,
        const float Tolerance)
    {
        // MHC/Outfit assembly can preserve UV topology while changing material asset paths,
        // render-pattern grouping, or section sizes. Those values are therefore useful as a
        // first-pass disambiguator, but they must not be required for correspondence.
        auto BuildBestForChannel = [&](const int32 Channel)
        {
            FMaps Strict = BuildRenderMapsForChannel(Source, Target, Channel, Tolerance, true);
            FMaps UVOnly = BuildRenderMapsForChannel(Source, Target, Channel, Tolerance, false);
            return UVOnly.MatchedFaces > Strict.MatchedFaces ? MoveTemp(UVOnly) : MoveTemp(Strict);
        };

        if (RequestedChannel >= 0)
        {
            return BuildBestForChannel(RequestedChannel);
        }

        int32 MaxChannels = 0;
        for (const TArray<FVector2f>& UVSet : Source.GetRenderUVs())
        {
            MaxChannels = FMath::Max(MaxChannels, UVSet.Num());
        }
        for (const TArray<FVector2f>& UVSet : Target.GetRenderUVs())
        {
            MaxChannels = FMath::Min(MaxChannels == 0 ? UVSet.Num() : MaxChannels, UVSet.Num());
        }
        MaxChannels = FMath::Max(MaxChannels, 1);

        FMaps Best;
        for (int32 Channel = 0; Channel < MaxChannels; ++Channel)
        {
            FMaps Candidate = BuildBestForChannel(Channel);
            if (Candidate.MatchedFaces > Best.MatchedFaces)
            {
                Best = MoveTemp(Candidate);
            }
        }
        return Best;
    }

    static FMaps BuildSimMaps(
        const FCollectionClothConstFacade& Source,
        const FCollectionClothConstFacade& Target,
        const float Tolerance)
    {
        FMaps Result;
        const TConstArrayView<FIntVector3> SourceFaces = Source.GetSimIndices2D();
        const TConstArrayView<FIntVector3> TargetFaces = Target.GetSimIndices2D();
        const TConstArrayView<FVector2f> Source2D = Source.GetSimPosition2D();
        const TConstArrayView<FVector2f> Target2D = Target.GetSimPosition2D();
        const TConstArrayView<FVector3f> Source3D = Source.GetSimPosition3D();
        const TConstArrayView<FVector3f> Target3D = Target.GetSimPosition3D();
        const TArray<FName> SourceTags = BuildSimFaceTags(Source);
        const TArray<FName> TargetTags = BuildSimFaceTags(Target);

        Result.Face.Init(INDEX_NONE, SourceFaces.Num());
        Result.Vertex.Init(INDEX_NONE, Source2D.Num());

        TMap<FTriKey, TArray<int32>> TargetByKey;
        for (int32 FaceIndex = 0; FaceIndex < TargetFaces.Num(); ++FaceIndex)
        {
            const FIntVector3& Tri = TargetFaces[FaceIndex];
            if (!Target2D.IsValidIndex(Tri[0]) || !Target2D.IsValidIndex(Tri[1]) || !Target2D.IsValidIndex(Tri[2]))
            {
                continue;
            }
            TargetByKey.FindOrAdd(MakeTriKey(TargetTags.IsValidIndex(FaceIndex) ? TargetTags[FaceIndex] : NAME_None,
                Target2D[Tri[0]], Target2D[Tri[1]], Target2D[Tri[2]], Tolerance)).Add(FaceIndex);
        }

        FVector3f SourceMin, SourceMax, TargetMin, TargetMax;
        ComputeBounds(Source3D, SourceMin, SourceMax);
        ComputeBounds(Target3D, TargetMin, TargetMax);

        TSet<int32> UsedTargetFaces;
        TArray<TMap<int32, int32>> VertexVotes;
        VertexVotes.SetNum(Source2D.Num());

        const TConstArrayView<FIntVector3> SourceFaces3D = Source.GetSimIndices3D();
        const TConstArrayView<FIntVector3> TargetFaces3D = Target.GetSimIndices3D();

        for (int32 SourceFaceIndex = 0; SourceFaceIndex < SourceFaces.Num(); ++SourceFaceIndex)
        {
            const FIntVector3& SourceTri = SourceFaces[SourceFaceIndex];
            if (!Source2D.IsValidIndex(SourceTri[0]) || !Source2D.IsValidIndex(SourceTri[1]) || !Source2D.IsValidIndex(SourceTri[2]))
            {
                continue;
            }
            const FVector2f SourceUV[3] = { Source2D[SourceTri[0]], Source2D[SourceTri[1]], Source2D[SourceTri[2]] };
            const FTriKey Key = MakeTriKey(SourceTags.IsValidIndex(SourceFaceIndex) ? SourceTags[SourceFaceIndex] : NAME_None,
                SourceUV[0], SourceUV[1], SourceUV[2], Tolerance);
            const TArray<int32>* Candidates = TargetByKey.Find(Key);
            if (!Candidates || Candidates->IsEmpty())
            {
                continue;
            }

            FVector3f SourceCentroidN = FVector3f::ZeroVector;
            if (SourceFaces3D.IsValidIndex(SourceFaceIndex))
            {
                SourceCentroidN = NormalizePosition(FaceCentroid(Source3D, SourceFaces3D[SourceFaceIndex]), SourceMin, SourceMax);
            }

            int32 BestTargetFace = INDEX_NONE;
            float BestScore = TNumericLimits<float>::Max();
            for (const int32 Candidate : *Candidates)
            {
                if (UsedTargetFaces.Contains(Candidate) || !TargetFaces.IsValidIndex(Candidate))
                {
                    continue;
                }
                float Score = 0.f;
                if (TargetFaces3D.IsValidIndex(Candidate))
                {
                    const FVector3f TargetCentroidN = NormalizePosition(FaceCentroid(Target3D, TargetFaces3D[Candidate]), TargetMin, TargetMax);
                    Score = FVector3f::DistSquared(SourceCentroidN, TargetCentroidN);
                }
                if (Score < BestScore)
                {
                    BestScore = Score;
                    BestTargetFace = Candidate;
                }
            }
            if (BestTargetFace == INDEX_NONE)
            {
                continue;
            }

            UsedTargetFaces.Add(BestTargetFace);
            Result.Face[SourceFaceIndex] = BestTargetFace;
            ++Result.MatchedFaces;

            const FIntVector3& TargetTri = TargetFaces[BestTargetFace];
            const FVector2f TargetUV[3] = { Target2D[TargetTri[0]], Target2D[TargetTri[1]], Target2D[TargetTri[2]] };
            int32 Perm[3];
            BestCornerPermutation(SourceUV, TargetUV, Perm);
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                ++VertexVotes[SourceTri[Corner]].FindOrAdd(TargetTri[Perm[Corner]]);
            }
        }

        for (int32 SourceVertex = 0; SourceVertex < VertexVotes.Num(); ++SourceVertex)
        {
            int32 BestTarget = INDEX_NONE;
            int32 BestVotes = 0;
            for (const TPair<int32, int32>& Pair : VertexVotes[SourceVertex])
            {
                if (Pair.Value > BestVotes)
                {
                    BestVotes = Pair.Value;
                    BestTarget = Pair.Key;
                }
            }
            Result.Vertex[SourceVertex] = BestTarget;
        }
        return Result;
    }

    static TArray<int32> BuildSim3DVertexMap(
        const FCollectionClothConstFacade& Source,
        const FCollectionClothConstFacade& Target,
        const TArray<int32>& Sim2DVertexMap)
    {
        TArray<int32> Result;
        Result.Init(INDEX_NONE, Source.GetSimPosition3D().Num());
        const TConstArrayView<int32> SourceLookup = Source.GetSimVertex3DLookup();
        const TConstArrayView<int32> TargetLookup = Target.GetSimVertex3DLookup();
        TArray<TMap<int32, int32>> Votes;
        Votes.SetNum(Result.Num());

        for (int32 Source2DVertex = 0; Source2DVertex < Sim2DVertexMap.Num(); ++Source2DVertex)
        {
            const int32 Target2DVertex = Sim2DVertexMap[Source2DVertex];
            if (!SourceLookup.IsValidIndex(Source2DVertex) || !TargetLookup.IsValidIndex(Target2DVertex))
            {
                continue;
            }
            const int32 Source3DVertex = SourceLookup[Source2DVertex];
            const int32 Target3DVertex = TargetLookup[Target2DVertex];
            if (Votes.IsValidIndex(Source3DVertex) && Target3DVertex != INDEX_NONE)
            {
                ++Votes[Source3DVertex].FindOrAdd(Target3DVertex);
            }
        }

        for (int32 SourceVertex = 0; SourceVertex < Votes.Num(); ++SourceVertex)
        {
            int32 BestTarget = INDEX_NONE;
            int32 BestVotes = 0;
            for (const TPair<int32, int32>& Pair : Votes[SourceVertex])
            {
                if (Pair.Value > BestVotes)
                {
                    BestVotes = Pair.Value;
                    BestTarget = Pair.Key;
                }
            }
            Result[SourceVertex] = BestTarget;
        }
        return Result;
    }

    static void RemapSet(const TSet<int32>& SourceSet, const TArray<int32>& IndexMap, TSet<int32>& OutSet)
    {
        OutSet.Reset();
        for (const int32 SourceIndex : SourceSet)
        {
            if (IndexMap.IsValidIndex(SourceIndex) && IndexMap[SourceIndex] != INDEX_NONE)
            {
                OutSet.Add(IndexMap[SourceIndex]);
            }
        }
    }


    static int32 RemapFloatValues(
        const TConstArrayView<float> SourceValues,
        const TArray<int32>& SourceToTarget,
        const int32 TargetCount,
        TArray<float>& OutValues)
    {
        OutValues.Init(0.f, TargetCount);
        TArray<int32> Counts;
        Counts.Init(0, TargetCount);
        int32 MappedSourceCount = 0;

        const int32 NumSource = FMath::Min(SourceValues.Num(), SourceToTarget.Num());
        for (int32 SourceIndex = 0; SourceIndex < NumSource; ++SourceIndex)
        {
            const int32 TargetIndex = SourceToTarget[SourceIndex];
            if (!OutValues.IsValidIndex(TargetIndex))
            {
                continue;
            }

            OutValues[TargetIndex] += SourceValues[SourceIndex];
            ++Counts[TargetIndex];
            ++MappedSourceCount;
        }

        for (int32 TargetIndex = 0; TargetIndex < OutValues.Num(); ++TargetIndex)
        {
            if (Counts[TargetIndex] > 1)
            {
                OutValues[TargetIndex] /= static_cast<float>(Counts[TargetIndex]);
            }
        }
        return MappedSourceCount;
    }
}

FChaosClothUniversalSelectionRemapNode::FChaosClothUniversalSelectionRemapNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
    : FDataflowNode(InParam, InGuid)
{
    RegisterInputConnection(&SourceCollection);
    RegisterInputConnection(&Collection);
    RegisterOutputConnection(&Collection, &Collection);
}

void FChaosClothUniversalSelectionRemapNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
    using namespace UE::Chaos::ClothAsset;
    using namespace UE::Chaos::ClothAsset::UniversalSelectionRemap;

    if (!Out->IsA<FManagedArrayCollection>(&Collection))
    {
        return;
    }

    FManagedArrayCollection SourceValue = GetValue<FManagedArrayCollection>(Context, &SourceCollection);
    FManagedArrayCollection TargetValue = GetValue<FManagedArrayCollection>(Context, &Collection);
    const TSharedRef<FManagedArrayCollection> SourceCollectionRef = MakeShared<FManagedArrayCollection>(MoveTemp(SourceValue));
    const TSharedRef<FManagedArrayCollection> TargetCollectionRef = MakeShared<FManagedArrayCollection>(MoveTemp(TargetValue));

    const FCollectionClothConstFacade SourceFacade(SourceCollectionRef);
    FCollectionClothFacade TargetFacade(TargetCollectionRef);
    const FCollectionClothSelectionConstFacade SourceSelections(SourceCollectionRef);
    FCollectionClothSelectionFacade TargetSelections(TargetCollectionRef);

    if (!SourceFacade.IsValid() || !TargetFacade.IsValid() || !SourceSelections.IsValid())
    {
        Context.Warning(LOCTEXT("InvalidCollections", "Source/target is not a valid Cloth Collection or the source has no selection schema."), this, Out);
        SetValue(Context, MoveTemp(*TargetCollectionRef), &Collection);
        return;
    }

    TargetSelections.DefineSchema();

    const float SafeTolerance = FMath::Max(CoordinateTolerance, 1.e-7f);
    const FMaps RenderMaps = BuildRenderMaps(SourceFacade, TargetFacade, RenderUVChannel, SafeTolerance);
    const FMaps SimMaps = BuildSimMaps(SourceFacade, TargetFacade, SafeTolerance);
    const TArray<int32> Sim3DVertexMap = BuildSim3DVertexMap(SourceFacade, TargetFacade, SimMaps.Vertex);

    if (bRemapSimulationWeightMaps)
    {
        const TArray<FName> WeightMapNames = SourceFacade.GetWeightMapNames();
        for (const FName& WeightMapName : WeightMapNames)
        {
            const TConstArrayView<float> SourceWeights = SourceFacade.GetWeightMap(WeightMapName);
            if (SourceWeights.Num() != SourceFacade.GetNumSimVertices3D())
            {
                continue;
            }

            TArray<float> RemappedWeights;
            const int32 Mapped = RemapFloatValues(
                SourceWeights, Sim3DVertexMap, TargetFacade.GetNumSimVertices3D(), RemappedWeights);

            TargetFacade.AddWeightMap(WeightMapName);
            TArrayView<float> TargetWeights = TargetFacade.GetWeightMap(WeightMapName);
            if (TargetWeights.Num() == RemappedWeights.Num())
            {
                for (int32 Index = 0; Index < RemappedWeights.Num(); ++Index)
                {
                    TargetWeights[Index] = RemappedWeights[Index];
                }
            }

            const float Ratio = SourceWeights.IsEmpty() ? 1.f : static_cast<float>(Mapped) / static_cast<float>(SourceWeights.Num());
            if (Ratio < WarnBelowMatchRatio)
            {
                Context.Warning(
                    FText::Format(
                        LOCTEXT("LowWeightMapCoverage", "Weight map '{0}' remapped {1}/{2} simulation vertices ({3}%)."),
                        FText::FromName(WeightMapName),
                        FText::AsNumber(Mapped),
                        FText::AsNumber(SourceWeights.Num()),
                        FText::AsNumber(FMath::RoundToInt(Ratio * 100.f))),
                    this, Out);
            }

            if (WeightMapName == FName(TEXT("MaxDistance")) && !RemappedWeights.IsEmpty())
            {
                float MinValue = TNumericLimits<float>::Max();
                float MaxValue = -TNumericLimits<float>::Max();
                for (const float Value : RemappedWeights)
                {
                    MinValue = FMath::Min(MinValue, Value);
                    MaxValue = FMath::Max(MaxValue, Value);
                }
                if (MaxValue <= UE_SMALL_NUMBER)
                {
                    Context.Warning(LOCTEXT("MaxDistanceAllZero", "Remapped MaxDistance is all zero. Cloth will be fully kinematic and will not simulate."), this, Out);
                }
            }
        }
    }

    if (bRemapRenderFloatAttributes)
    {
        const TArray<FName> RenderFloatNames = SourceFacade.GetUserDefinedAttributeNames<float>(ClothCollectionGroup::RenderVertices);
        for (const FName& AttributeName : RenderFloatNames)
        {
            // ProxyDeformer regenerates this internal map for the fitted geometry.
            if (AttributeName == FName(TEXT("_SkinningBlendWeight")))
            {
                continue;
            }

            const TConstArrayView<float> SourceValues = SourceFacade.GetUserDefinedAttribute<float>(AttributeName, ClothCollectionGroup::RenderVertices);
            if (SourceValues.Num() != SourceFacade.GetNumRenderVertices())
            {
                continue;
            }

            TArray<float> RemappedValues;
            const int32 Mapped = RemapFloatValues(
                SourceValues, RenderMaps.Vertex, TargetFacade.GetNumRenderVertices(), RemappedValues);

            TargetFacade.AddUserDefinedAttribute<float>(AttributeName, ClothCollectionGroup::RenderVertices);
            TArrayView<float> TargetValues = TargetFacade.GetUserDefinedAttribute<float>(AttributeName, ClothCollectionGroup::RenderVertices);
            if (TargetValues.Num() == RemappedValues.Num())
            {
                for (int32 Index = 0; Index < RemappedValues.Num(); ++Index)
                {
                    TargetValues[Index] = RemappedValues[Index];
                }
            }

            const float Ratio = SourceValues.IsEmpty() ? 1.f : static_cast<float>(Mapped) / static_cast<float>(SourceValues.Num());
            if (Ratio < WarnBelowMatchRatio)
            {
                Context.Warning(
                    FText::Format(
                        LOCTEXT("LowRenderFloatCoverage", "Render float map '{0}' remapped {1}/{2} vertices ({3}%)."),
                        FText::FromName(AttributeName),
                        FText::AsNumber(Mapped),
                        FText::AsNumber(SourceValues.Num()),
                        FText::AsNumber(FMath::RoundToInt(Ratio * 100.f))),
                    this, Out);
            }
        }
    }

    int32 RemappedSelectionCount = 0;
    for (const FName& SelectionName : SourceSelections.GetNames())
    {
        if (!bOverwriteExistingSelections && TargetSelections.HasSelection(SelectionName))
        {
            continue;
        }

        const FName Group = SourceSelections.GetSelectionGroup(SelectionName);
        const TSet<int32>& SourceSet = SourceSelections.GetSelectionSet(SelectionName);
        TSet<int32> NewSet;
        bool bSupported = true;

        if (Group == ClothCollectionGroup::RenderFaces)
        {
            RemapSet(SourceSet, RenderMaps.Face, NewSet);
        }
        else if (Group == ClothCollectionGroup::RenderVertices)
        {
            RemapSet(SourceSet, RenderMaps.Vertex, NewSet);
        }
        else if (Group == ClothCollectionGroup::SimFaces)
        {
            RemapSet(SourceSet, SimMaps.Face, NewSet);
        }
        else if (Group == ClothCollectionGroup::SimVertices2D)
        {
            RemapSet(SourceSet, SimMaps.Vertex, NewSet);
        }
        else if (Group == ClothCollectionGroup::SimVertices3D)
        {
            RemapSet(SourceSet, Sim3DVertexMap, NewSet);
        }
        else if (bCopyUnsupportedGroupsByIndexWhenSameSize &&
                 SourceCollectionRef->NumElements(Group) == TargetCollectionRef->NumElements(Group))
        {
            NewSet = SourceSet;
        }
        else
        {
            bSupported = false;
        }

        if (!bSupported)
        {
            Context.Warning(
                FText::Format(LOCTEXT("UnsupportedGroup", "Selection '{0}' uses unsupported group '{1}' and was not copied."), FText::FromName(SelectionName), FText::FromName(Group)),
                this, Out);
            continue;
        }

        TSet<int32>& TargetSet = TargetSelections.FindOrAddSelectionSet(SelectionName, Group);
        TargetSet = MoveTemp(NewSet);
        ++RemappedSelectionCount;

        if (!SourceSet.IsEmpty())
        {
            const float Ratio = static_cast<float>(TargetSet.Num()) / static_cast<float>(SourceSet.Num());
            if (Ratio < WarnBelowMatchRatio)
            {
                Context.Warning(
                    FText::Format(
                        LOCTEXT("LowSelectionCoverage", "Selection '{0}' remapped {1}/{2} elements ({3}%). Check UV/pattern correspondence or lower Coordinate Tolerance only if the UVs truly differ."),
                        FText::FromName(SelectionName),
                        FText::AsNumber(TargetSet.Num()),
                        FText::AsNumber(SourceSet.Num()),
                        FText::AsNumber(FMath::RoundToInt(Ratio * 100.f))),
                    this, Out);
            }
        }
    }

    if (RemappedSelectionCount == 0)
    {
        Context.Warning(LOCTEXT("NoSelectionsRemapped", "No selections were remapped."), this, Out);
    }

    SetValue(Context, MoveTemp(*TargetCollectionRef), &Collection);
}

#undef LOCTEXT_NAMESPACE
