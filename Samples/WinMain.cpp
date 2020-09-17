#include <Platform/IBPlatform.h>

int main()
{
	IB::WindowDesc winDesc;
	winDesc.Name = "Ice Box";
	winDesc.Width = 500;
	winDesc.Height = 150;
	IB::Window* window = IB::createWindow(winDesc);

	while (IB::tickWindow(window) != IB::WindowState::Closing)
	{

	}

	IB::destroyWindow(window);
}
