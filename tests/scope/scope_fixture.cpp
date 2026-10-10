#include "scope_widget.h"

// the build-scope fixture, which the check compiles alongside the generated unit (§14.3)
const albor::ScopeWidget* scope_fixture();

// the frame consumes only the facade interface, which is what the header change recomplies (§14.3)
const albor::ScopeWidget* scope_fixture() {
    // nothing is ever constructed, because the fixture only measures the compile set (§14.3)
    return nullptr;
}
