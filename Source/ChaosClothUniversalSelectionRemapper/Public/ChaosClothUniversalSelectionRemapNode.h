#pragma once

#include "Dataflow/DataflowNode.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "ChaosClothUniversalSelectionRemapNode.generated.h"

/**
 * Copies every named Chaos Cloth selection from SourceCollection to Collection,
 * remapping indices by topology-independent garment identity rather than by raw index.
 *
 * Render selections are matched by render-pattern/material + UV-triangle signatures.
 * Simulation selections are matched by 2D pattern-space triangle signatures.
 * This is designed for fitted/reordered versions of the same garment/proxy (for example,
 * a MetaHuman Wardrobe-fitted render mesh and proxy whose raw face indices changed).
 */
USTRUCT(Meta = (DataflowCloth))
struct FChaosClothUniversalSelectionRemapNode : public FDataflowNode
{
    GENERATED_USTRUCT_BODY()
    DATAFLOW_NODE_DEFINE_INTERNAL(
        FChaosClothUniversalSelectionRemapNode,
        "UniversalClothSelectionRemap",
        "Cloth",
        "Universal Cloth Selection Remap")

public:
    /** Original cloth collection containing the correct named selections. */
    UPROPERTY(Meta = (DataflowInput))
    FManagedArrayCollection SourceCollection;

    /** New/fitted cloth collection. All remapped selections are written here. */
    UPROPERTY(Meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection"))
    FManagedArrayCollection Collection;

    /** UV channel used to identify render triangles. -1 tries all available channels and keeps the best match. */
    UPROPERTY(EditAnywhere, Category = "Universal Remap", Meta = (ClampMin = "-1"))
    int32 RenderUVChannel = -1;

    /** Quantization tolerance for render UV and simulation 2D coordinates. */
    UPROPERTY(EditAnywhere, Category = "Universal Remap", Meta = (ClampMin = "0.0000001", UIMin = "0.000001", UIMax = "0.001"))
    float CoordinateTolerance = 0.00001f;

    /** Replace target selections that already have the same name. */
    UPROPERTY(EditAnywhere, Category = "Universal Remap")
    bool bOverwriteExistingSelections = true;

    /** Remap all simulation weight maps (MaxDistance, Backstop, AnimDrive, etc.) using the exact SimVertices3D correspondence. */
    UPROPERTY(EditAnywhere, Category = "Universal Remap")
    bool bRemapSimulationWeightMaps = true;

    /** Remap user-defined float attributes on render vertices using render-vertex correspondence. */
    UPROPERTY(EditAnywhere, Category = "Universal Remap")
    bool bRemapRenderFloatAttributes = true;

    /** For non-standard selection groups, copy raw indices only when source and target group sizes are identical. */
    UPROPERTY(EditAnywhere, Category = "Universal Remap")
    bool bCopyUnsupportedGroupsByIndexWhenSameSize = false;

    /** Emit a Dataflow warning when fewer than this fraction of a selection can be remapped. */
    UPROPERTY(EditAnywhere, Category = "Universal Remap", Meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float WarnBelowMatchRatio = 0.95f;

    FChaosClothUniversalSelectionRemapNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

private:
    virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};
