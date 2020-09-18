# IceBox

## Coding Standard
- TODO: Move to new markdown file.
- TODO: Flesh the rest of this out.

### Naming
- External variables will be upper camel cased. (MyVariable) This includes member variables, global variables and static variables.
- Local variables will be lower camel cased. (myVariable) This includes local function variables and function arguments.
- Functions will be lower camel cased. (myFoo())
  - Exception: Function pointers are variables, not functions in the context of the coding standard.
- Types will be upper camel cased. (MyClass) This includes classes, structs, typedefs and using statements.
- Type prefixes and post fixes are disallowed. (m_var, ivar, pvar, var_g, etc)

### Namespaces
- All engine code will be included in the IB namespace.
- Namespaces can be nested, but be considerate. Too many namespaces made programming no fun.

### Headers
- Use #pragma once as include guards.
- Using namespace statements in headers are disallowed.
- A header shall not depend on headers being included before it in a cpp file.

### Includes
- Unnecessary includes for the contents of a headers are disallowed. Even for convenience.
- Use quoted includes for files within the same project. "../MyHeader.h"
- Use bracket includes for files that cross project boundaries. <ProjectA/MyHeader.h>

### Formatting
- Defer to the configured visual studio formatting rules. (Simply 'ctrl+a' 'ctrl+k, f' to format a file)
