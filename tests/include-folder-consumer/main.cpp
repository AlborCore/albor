// the include folder consumer, which locks the albor/ resolution beside a consumer core root (#152, §3.5, AD-19)

#include <cstdio>

#include "albor/core/geometry.h"
#include "core/conflict.h"

int main() {
    // the framework size resolves through the albor folder, which the consumer's own core cannot shadow
    const albor::Size size{320.0f, 240.0f};
    // the consumer core header resolves beside it, which is the coexistence the issue asked for
    std::printf("albor size %gx%g, consumer marker %d\n", size.width, size.height, consumer_marker());
    return consumer_marker() == 7 && size.width == 320.0f ? 0 : 1;
}
