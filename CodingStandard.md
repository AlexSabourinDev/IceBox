## Coding Standard
- TODO: Flesh the rest of this out.

## C++

### File And Folder Naming
- Engine files will be prefixed with IB.
- Top level engine folders will be prefixed with IB.
- Sample and game files should not use the IB prefix.

### Module Structure
- Systems/Modules should be contained in as few files as necessary.
  - Many small files impacts compile time overhead.
  - Many small files increase cognitive overhead.
  - Single files per classes are discouraged.
- Module header files should contain as minimal of an API as the module needs to function.
  - Begin with a coarser API and refine it based on use-case.
- Implementation details should remain in cpp files as much as possible.
  - This does mean that private members to a class are discouraged. Consider if the class needs to be in a header at all.
  - Includes that are only relevant to the implementation should be in a cpp file as much as possible.

### Naming
- External variables will be upper camel cased. (MyVariable) This includes member variables, global variables and static variables.
- Local variables will be lower camel cased. (myVariable) This includes local function variables and function arguments.
- Functions will be lower camel cased. (myFoo())
  - Exception: Function pointers are variables, not functions in the context of the coding standard.
- Types will be upper camel cased. (MyClass) This includes classes, structs, typedefs and using statements.
- Type prefixes and post fixes are disallowed. (m_var, ivar, pvar, var_g, etc)

### Namespaces
- All engine code will be included in the IB namespace.
- Namespaces can be nested, but be considerate. Too many namespaces make programming no fun.

### Headers
- Use #pragma once as include guards.
- Using namespace statements in headers are disallowed.
- A header shall not depend on headers being included before it in a cpp file.
- Private static functions in headers are discouraged.

### Includes
- Unnecessary includes for the contents of a headers are disallowed. Even for convenience.
- Use quoted includes for files within the same project. "../MyHeader.h"
- Use bracket includes for files that cross project boundaries. <ProjectA/MyHeader.h>

### Formatting
- Defer to the clang format file associated with the project. Visual Studio should pick it up automatically.

### Constness
- Apply const in a right to left fashion.
- int* const is a constant pointer to a mutable int
- int const* is a mutable pointer to a constant int
- int const* const is a constant pointer to a constant int

### Templates
- Prefer templates in .cpp files
- Attempt to minimize templates in .h files

### Argument Passing
- Use constant reference for immutable non-copied arguments
- Use pointers for mutable arguments
- Use value types for smaller values

### Disallowed
- mutable. mutable is threading unfriendly and can cause surprising behaviour.
- const_cast

### Typedefs
- Use typedefs sparingly. Consider: Do you really need this to be a typedef? Is it already a typedef? Please try not to double typedef.
