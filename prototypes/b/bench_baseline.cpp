#include "prototypes/b/baseline/Component000.h"
#include "prototypes/b/baseline/Component499.h"

// exercise the baseline template components to force compilation of the header chain
void bench_baseline_exercise() {
    Component000<int> base0;
    base0.set_color(1.0F);
    base0.set_opacity(0.5F);
    base0.on_click();

    Component499<int> base499;
    base499.set_width(100.0F);
    base499.set_height(200.0F);
    base499.on_focus();
}
