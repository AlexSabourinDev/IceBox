# Engine Architecture

## Layers
The engine will be split into roughly 3 conceptual layers. These layers represent the flow of dependencies.

In order from least dependent to most dependent we have:
* The core layer
* The service layer
* The frontend layer

The service layer depends on the core layer and the frontend layer depends on both the service and core layers.

Some example of modules in each layer are:
**Core Layer**
* Allocator
* Job System
* Platform
* Logging
* IO
* Math

**Service Layer**
* Rendering
* Physics
* Serialization
* Audio

**Frontend Layer**
* Rendering frontend
* Transform hierarchy
* Physics frontend
* Asset loader

The layers are not perfect abstractions, interdependence between layer modules is unavoidable but should be reduced. (Logging is likely to appear in many modules at every layer)

### Core Layer
The core layer represents core modules in use by the other engine layers. Things that are foundational such as job systems, allocators, platform abstraction, collections, etc. It should be easy to simply use a third party library as a core module if we wanted to.

### Service Layer
The service layer represent different services that are likely to be present in the engine such as rendering, physics, serialization, audio, etc. This layer represents different areas of functionality you might expect to see in an engine but is not directly interfacing with the engine's frontend. You should be able to use third party libraries like PhysX, BGFX and other libraries in this layer.

### Frontend Layer
The frontend layer is the layer that interacts most with game code, the editor and tools. The frontend communicates primarily with "assets". Assets are descriptions of data that can be fed to frontend systems to drive their functionality.

Some examples of assets are meshes, materials, components, transforms, etc.

The frontend layer is likely to interact with various parts of the service layer or individually. Ideally, if functionality gets large enough, separating a module into a frontend and service layer is likely a reasonable expectation to reduce the number of dependencies the core functionality would have on frontend portions of the engine.

Here is an example of how I expect the frontend to function:
- A cell is an asset that represents a collection of entities
- An entity is an asset that represents a collection of components
- A component describes data to be fed to the appropriate systems
- For example, a renderable component would describe a mesh and material pair
- The mesh and materials are assets that get fed to the rendering frontend.

On game startup
- The game requests a cell to be loaded
- The cell queues up loads of the entity assets
- The entity assets queue up loads of their components
- The components queue up loads of their data
- The data can then queue up a load of various other assets

Once an assets dependent loads are complete, a system is notified that an asset has been loaded. This implies that we have an event driven system to loading assets.

Taking the example of a renderable component, once it's mesh and material are loaded, the rendering frontend is notified that the renderable has been loaded. When the mesh and material had loaded, the rendering frontend was also notified to allow it to store the asset.

Once a renderable is loaded it's entity could be marked as loaded telling our entity system about our new entity. And finally, once our cell is loaded, it can prepare our entities for rendering.

## Asset storage
Assets will be primarily stored in JSON and compiled to a binary format by an offline content processing tool. Assets should be able to be both loaded from JSON and binary to avoid having to run the content processor for every game run.

The content processor would take our assets and package them up into a binary package alongside a header describing the contents of the package.

## Module style
I expect modules to be mainly a .h/.cpp pair. Reducing the number of files helps reduce compile times and conceptual overhead. We should split modules into multiple files reactively instead of proactively. Module APIs should be as minimal as possible, keeping our implementations within our cpp files.

## Rough Systems List
* Math
* Physics
* Rendering
* Jobs
* Allocator
* Serialization (JSON)
* Asset Loader
* Content Processor
* Rendering Frontend
* Physics Frontend
* Transform Hierarchy
* Audio
* Tools
* Profiler
* Memory Profiling
* Testing
* Scripting (Lua)
* Input
