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
    IB::continueJob([threadEvent]()
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

    bool const createAndSaveCell = false;
    if(createAndSaveCell)
    {
        IB::Cell::Handle cellHandle = IB::createCell();
        IB::Resource::Handle cellResource = IB::Asset::createResourceThreadSafe("TestCell.cell", IB::Asset::toFourCC("CELL"), IB::toAssetHandle(cellHandle));

        IB::MaterialAsset materialAsset;
        materialAsset.AlbedoPath = "bubbles.tex";
        materialAsset.AlbedoTint = IB::toRGBA(1.0f, 1.0f, 1.0f, 1.0f);
        IB::MaterialAssetHandle materialHandle = IB::createMaterialAsset(materialAsset);
        IB::Resource::Handle materialResource = IB::Asset::createResourceThreadSafe("Test.mat", IB::Asset::toFourCC("MATE"), IB::toAssetHandle(materialHandle));

        {
            IB::Entity::Handle entityHandle = IB::createEntity();
            IB::Property::Handle rendererProperty = IB::createRendererProperty("Box.msh", "Test.mat");
            IB::addPropertyToEntity(entityHandle, IB::Asset::toFourCC("RNDR"), rendererProperty);
            IB::addEntityToCell(cellHandle, entityHandle);
        }

        IB::Job::Handle saveJob = IB::Asset::saveResourceAsync(materialResource);
        waitOnJob(saveJob);

        saveJob = IB::Asset::saveResourceAsync(cellResource);
        waitOnJob(saveJob);

        IB::Asset::releaseResourceAsync(materialResource);
        IB::Asset::releaseResourceAsync(cellResource);
    }

    IB::Resource::Handle cellResource;
    IB::Job::Handle loadJob = IB::Asset::loadResourceAsync("TestCell.cell", IB::Asset::toFourCC("CELL"), &cellResource);
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

        uint8_t tempMemory[1024 * 1024];
        IB::StackAllocator allocator = IB::createStackAllocator(tempMemory, 1024 * 1024);
        IB::drawCellView(IB::gatherCellView(cellHandle, nullptr, &allocator));
    }

    IB::Asset::releaseResourceAsync(cellResource);

    IB::killEntitySystem();
    IB::killRendererFrontend();
    IB::destroyWindow(window);
    IB::killJobSystem();
    IB::Serialization::killSerialization();
}
