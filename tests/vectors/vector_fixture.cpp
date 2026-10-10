#include "vector_panel.h"

// the collection fixture, which exists so the test binary links a real generated collection facade (§8.1, §11.3)
const albor::VectorPanel* vector_fixture();

// the test binary constructs the facade itself, because only it owns the engines a component reads (§11.3)
const albor::VectorPanel* vector_fixture() {
    // nothing is constructed here, because the unit exists only to carry the generated translation units (§11.3)
    return nullptr;
}
