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

    {
        IB::EntityHandle entityHandle = IB::createEntity();
        IB::PropertyHandle rendererProperty = IB::createRendererProperty("Box.msh");
        IB::addPropertyToEntity(entityHandle, IB::Asset::toFourCC("RNDR"), rendererProperty);
        IB::addEntityToCell(cellHandle, entityHandle);
    }

    IB::Asset::releaseResourceAsync(cellResource);

    waitOnJob(rendererInit);
    IB::killEntitySystem();
    IB::killRendererFrontend();
    IB::destroyWindow(window);
    IB::killJobSystem();
    IB::Serialization::killSerialization();
}
