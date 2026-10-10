#pragma once

#include <vector>

namespace albor
{
    // the sampled waveform the chart draws, which the manifest names as the data type of the native view (§11.2, AD-17)
    struct WaveformData
    {
        // the amplitude samples in drawing order, which a painted line strokes (§6.4)
        std::vector<float> samples;
    };
} // namespace albor
