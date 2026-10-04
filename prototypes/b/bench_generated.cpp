#include "prototypes/b/generated_suite/Component000.h"
#include "prototypes/b/generated_suite/Component499.h"

// exercise the generated facade components to force compilation of the library
void bench_generated_exercise() {
    Component000 gen0;
    gen0.set_color(1.0F);
    gen0.set_opacity(0.5F);
    gen0.on_click();

    Component499 gen499;
    gen499.set_width(100.0F);
    gen499.set_height(200.0F);
    gen499.on_focus();
}
