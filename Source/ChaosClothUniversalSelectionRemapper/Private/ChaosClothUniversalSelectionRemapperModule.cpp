#include "ChaosClothUniversalSelectionRemapNode.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Modules/ModuleManager.h"

class FChaosClothUniversalSelectionRemapperModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FChaosClothUniversalSelectionRemapNode);
    }
};

IMPLEMENT_MODULE(FChaosClothUniversalSelectionRemapperModule, ChaosClothUniversalSelectionRemapper)
