#include <IBEngine/IBSerialization.h>
#include <IBEngine/IBJobs.h>
#include <IBEngine/IBEntity.h>
#include <IBEngine/IBRendererFrontend.h>
#include <IBEngine/IBRenderer.h>
#include <IBEngine/IBAllocator.h>
#include <IBEngine/IBAsset.h>
#include <IBEngine/IBPlatform.h>

void waitOnJob(IB::Job::Handle job)
{
    IB::ThreadEvent threadEvent = IB::createThreadEvent();
    IB::continueJob([threadEvent](Job::Handle /*currentJob*/)
    {
        IB::signalThreadEvent(threadEvent);
        return IB::JobResult::Complete;
    },
        &job, 1);
    IB::waitOnThreadEvent(threadEvent);
    IB::destroyThreadEvent(threadEvent);
};

int main()
{
    IB::WindowDesc winDesc = {};
    winDesc.Name = "Ice Box";
    winDesc.Width = 1024;
    winDesc.Height = 720;
    winDesc.OnWindowMessage = [](void * /*data*/, IB::WindowMessage message, IB::WindowMessagePlatformData const* platformData)
    {
        switch (message.Type)
        {
        case IB::WindowMessage::Close:
            IB::sendQuitMessage();
            return true;
        }

        return false;
    };
    IB::Window::Handle window = IB::createWindow(winDesc);

    IB::Serialization::initSerialization();
    IB::initJobSystem();
    IB::initRendererFrontend({ window });
    IB::initEntitySystem();

    bool const createAndSaveCell = true;
    if(createAndSaveCell)
    {
        IB::Cell::Handle cellHandle = IB::createCell();
        IB::Resource::Handle cellResource = IB::Asset::createResourceThreadSafe("TestCell.cell", IB::CellType, IB::toAssetHandle(cellHandle));

        IB::MaterialAsset::Desc materialAsset;
        materialAsset.AlbedoPath = "bubbles.tex";
        materialAsset.AlbedoTint = IB::toRGBA(1.0f, 1.0f, 1.0f, 1.0f);
        IB::MaterialAsset::Handle materialHandle = IB::createMaterialAsset(materialAsset);
        IB::Resource::Handle materialResource = IB::Asset::createResourceThreadSafe("Test.mat", IB::MaterialType, IB::toAssetHandle(materialHandle));

        {
            IB::Entity::Handle entityHandle = IB::createEntity();
            IB::setEntityName(entityHandle, "EntityName01");
            IB::Property::Handle rendererProperty = IB::createRendererProperty("Box.msh", "Test.mat");
            IB::addPropertyToEntity(entityHandle, IB::RendererPropertyType, rendererProperty);
            IB::addEntityToCell(cellHandle, entityHandle);
        }

        IB::Job::Handle saveJob = IB::Asset::saveResourceAsync(materialResource);
        waitOnJob(saveJob);

        saveJob = IB::Asset::saveResourceAsync(cellResource);
        waitOnJob(saveJob);

        IB::Cell::Handle derivedCellHandle = IB::createCell(cellResource);
        IB::Resource::Handle derivedCellResource = IB::Asset::createResourceThreadSafe("TestDerivedCell.cell", IB::CellType, IB::toAssetHandle(derivedCellHandle));

        {
            IB::Entity::Handle entityHandle = IB::createEntity();
            IB::setEntityName(entityHandle, "EntityName02");
            IB::Property::Handle rendererProperty = IB::createRendererProperty("Box.msh", "Test.mat");
            IB::addPropertyToEntity(entityHandle, IB::RendererPropertyType, rendererProperty);
            IB::addEntityToCell(derivedCellHandle, entityHandle);
        }

        {
            IB::Entity::Handle *entities;
            uint32_t entityCount;

            IB::getEntityList(derivedCellHandle, &entities, &entityCount);
            for (uint32_t i = 0; i < entityCount; i++)
            {
                if (strcmp(IB::getEntityName(entities[i]), "EntityName01") == 0)
                {
                    IB::setEntityName(entities[i], "EntityName01_Derived");
                    break;
                }
            }
        }

        saveJob = IB::Asset::saveResourceAsync(derivedCellResource);
        waitOnJob(saveJob);

        IB::Asset::releaseResourceAsync(materialResource);
        IB::Asset::releaseResourceAsync(cellResource);
        IB::Asset::releaseResourceAsync(derivedCellResource);
    }

    IB::Resource::Handle cellResource;
    IB::Job::Handle loadJob = IB::Asset::loadResourceAsync("TestDerivedCell.cell", { IB::CellType }, &cellResource);
    waitOnJob(loadJob);
    IB::Cell::Handle cellHandle = IB::toCellHandle(IB::Asset::getAssetFromResource(cellResource));

    IB::PlatformMessage message = IB::PlatformMessage::None;
    while (message != IB::PlatformMessage::Quit)
    {
        IB::consumeMessageQueue([](void *data, IB::PlatformMessage message)
        {
            *reinterpret_cast<IB::PlatformMessage *>(data) = message;
        },
            &message);

        uint8_t tempMemory[1024];
        IB::StackAllocator allocator = IB::createStackAllocator(tempMemory, 1024);

        IB::GatherCellViewDesc gatherDesc
        {
            &allocator,
            IB::InvalidEntity,
            cellHandle,
            nullptr,
            nullptr
        };
        IB::drawCellView(&allocator, IB::gatherCellView(gatherDesc));
    }

    IB::Asset::releaseResourceAsync(cellResource);

    IB::killEntitySystem();
    IB::killRendererFrontend();
    IB::destroyWindow(window);
    IB::killJobSystem();
    IB::Serialization::killSerialization();
}
