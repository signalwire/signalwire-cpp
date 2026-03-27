# Migrating to SignalWire SDK 2.0

## Library and Header Renames

Update your `CMakeLists.txt`:
```cmake
# Before
find_package(signalwire_agents REQUIRED)
target_link_libraries(my_app PRIVATE signalwire_agents::signalwire_agents)

# After
find_package(signalwire REQUIRED)
target_link_libraries(my_app PRIVATE signalwire::signalwire)
```

## Include Changes

```cpp
// Before
#include <signalwire_agents.hpp>
#include <signalwire_agents_c.h>  // C API

signalwire::agents::AgentBase agent;
auto client = signalwire::agents::rest::SignalWireClient(project_id, token, space_url);

// After
#include <signalwire.hpp>
#include <signalwire_c.h>  // C API

signalwire::AgentBase agent;
auto client = signalwire::rest::RestClient(project_id, token, space_url);
```

## Class Renames

| Before | After |
|--------|-------|
| `signalwire::agents::AgentBase` | `signalwire::AgentBase` |
| `signalwire::agents::rest::SignalWireClient` | `signalwire::rest::RestClient` |
| `signalwire::agents::*` (all namespaces) | `signalwire::*` |
| `signalwire_agents.hpp` | `signalwire.hpp` |
| `signalwire_agents_c.h` | `signalwire_c.h` |

## Quick Migration

Find and replace in your project:
```bash
# Update header includes
find . -name '*.cpp' -o -name '*.hpp' -o -name '*.h' | \
  xargs sed -i 's/signalwire_agents\.hpp/signalwire.hpp/g'
find . -name '*.cpp' -o -name '*.hpp' -o -name '*.h' | \
  xargs sed -i 's/signalwire_agents_c\.h/signalwire_c.h/g'

# Flatten namespace (remove ::agents level)
find . -name '*.cpp' -o -name '*.hpp' -o -name '*.h' | \
  xargs sed -i 's/signalwire::agents::/signalwire::/g'

# Rename client class
find . -name '*.cpp' -o -name '*.hpp' -o -name '*.h' | \
  xargs sed -i 's/SignalWireClient/RestClient/g'

# Update CMakeLists.txt
sed -i 's/signalwire_agents/signalwire/g' CMakeLists.txt
```

## What Didn't Change

- All method signatures (set_prompt_text, define_tool, add_skill, etc.)
- All parameter types and structs
- SWML output format
- RELAY protocol
- REST API paths
- Skills, contexts, DataMap -- all the same
