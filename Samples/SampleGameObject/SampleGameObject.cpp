#include <IBEngine/IBSerialization.h>
#include <IBEngine/IBJobs.h>
#include <IBEngine/IBEntity.h>
#include <IBEngine/IBRendererFrontend.h>

void waitOnJob(IB::JobHandle job)
{
    IB::ThreadEvent threadEvent = IB::createThreadEvent();
    continueJob([threadEvent]()
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
    winDesc.Width = 500;
    winDesc.Height = 500;
    winDesc.OnWindowMessage = [](void * /*data*/, IB::WindowMessage message)
    {
        switch (message.Type)
        {
        case IB::WindowMessage::Close:
            IB::sendQuitMessage();
            break;
        }
    };
    IB::WindowHandle window = IB::createWindow(winDesc);

    IB::Serialization::initSerialization();
    IB::initJobSystem();
    IB::JobHandle rendererInit = IB::initRendererFrontend({ &window });
    IB::initEntitySystem();

    IB::CellHandle cellHandle = IB::createCell();
    IB::Asset::ResourceHandle cellResource = IB::Asset::createResourceThreadSafe("TestCell.cell", IB::Asset::toFourCC("CELL"), IB::toAssetHandle(cellHandle));

    IB::MaterialAsset materialAsset;
    materialAsset.AlbedoPath = "bubbles.tex";
    materialAsset.AlbedoTint = IB::toRGBA(1.0f, 1.0f, 1.0f, 1.0f);
    IB::MaterialAssetHandle materialHandle = IB::createMaterialAsset(materialAsset);
    IB::Asset::ResourceHandle materialResource = IB::Asset::createResourceThreadSafe("Test.mat", IB::Asset::toFourCC("MATE"), IB::toAssetHandle(materialHandle));

    {
        IB::EntityHandle entityHandle = IB::createEntity();
        IB::PropertyHandle rendererProperty = IB::createRendererProperty("Box.msh", "Test.mat");
        IB::addPropertyToEntity(entityHandle, IB::Asset::toFourCC("RNDR"), rendererProperty);
        IB::addEntityToCell(cellHandle, entityHandle);
    }

    waitOnJob(rendererInit);

    IB::JobHandle lastDraw = {};

    IB::PlatformMessage message = IB::PlatformMessage::None;
    while (message != IB::PlatformMessage::Quit)
    {
        IB::consumeMessageQueue([](void *data, IB::PlatformMessage message)
        {
            *reinterpret_cast<IB::PlatformMessage *>(data) = message;
        },
            &message);

        IB::JobHandle currentDraw = drawCell(cellHandle);
        waitOnJob(lastDraw);
        lastDraw = currentDraw;
    }

    IB::Asset::releaseResourceAsync(materialResource);
    IB::Asset::releaseResourceAsync(cellResource);

    IB::killEntitySystem();
    IB::killRendererFrontend();
    IB::destroyWindow(window);
    IB::killJobSystem();
    IB::Serialization::killSerialization();
}
