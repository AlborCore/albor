// the custom render node of §11.1: the hit query that runs on the ui thread while the view moves
// the query walks the uniform grid built with the resources, so its cost is independent of the symbol count (§12.3)

#include "prototypes/a1/custom_render_node.h"

#include <cmath>
#include <cstddef>
#include <limits>

#include "prototypes/a1/resource_set.h"

namespace albor::a1
{

    CustomRenderNode::CustomRenderNode(ResourceSetPtr resources) :
        m_resources(std::move(resources)) {}

    HitResult CustomRenderNode::hit_test(const HitQuery& query) const {
        // a node without resources cannot be hit
        if (m_resources == nullptr) {
            return HitResult{};
        }

        // a zoomed out view can never be hit precisely, so report no hit rather than a wrong one
        if (m_view.scale <= 0.0F) {
            return HitResult{};
        }

        // the grid is the only index, so a set without one has nothing to walk
        if (m_resources->grid_dim <= 0 || m_resources->grid.empty()) {
            return HitResult{};
        }

        // map the pointer back into resource space, which is where the symbols live
        const float resource_x = m_view.unmap_x(query.x);
        const float resource_y = m_view.unmap_y(query.y);

        // the tolerance travels into resource space too, because that is the space the distances are measured in
        const float tolerance = query.tolerance / m_view.scale;

        // a symbol is filed under the cell of its centre, so the pointer may sit in a neighbouring cell; the
        // query therefore walks the three by three neighbourhood, which keeps the cost independent of the
        // symbol count while still finding every symbol a pointer can reach (§12.3)
        const auto dimension = static_cast<std::size_t>(m_resources->grid_dim);
        const std::size_t centre_cell = m_resources->cell_of(resource_x, resource_y);
        const std::size_t centre_x = centre_cell % dimension;
        const std::size_t centre_y = centre_cell / dimension;

        // walk the neighbourhood and keep the closest candidate that was inside the tolerance
        HitResult best{};
        float best_distance = std::numeric_limits<float>::max();
        for (std::ptrdiff_t step_y = -1; step_y <= 1; step_y++) {
            for (std::ptrdiff_t step_x = -1; step_x <= 1; step_x++) {
                // skip the neighbour that falls outside the grid
                const std::ptrdiff_t neighbour_x = static_cast<std::ptrdiff_t>(centre_x) + step_x;
                const std::ptrdiff_t neighbour_y = static_cast<std::ptrdiff_t>(centre_y) + step_y;
                if (neighbour_x < 0 || neighbour_y < 0 || static_cast<std::size_t>(neighbour_x) >= dimension || static_cast<std::size_t>(neighbour_y) >= dimension) {
                    continue;
                }

                // read the bucket this neighbour holds
                const GridCell& bucket = m_resources->grid.at(static_cast<std::size_t>(neighbour_y) * dimension + static_cast<std::size_t>(neighbour_x));
                for (std::uint32_t offset = 0; offset < bucket.count; offset++) {
                    // read the symbol index the cell reserved for this candidate
                    const std::uint32_t index = m_resources->grid_indices.at(static_cast<std::size_t>(bucket.first) + static_cast<std::size_t>(offset));
                    const Symbol& symbol = m_resources->symbols.at(index);

                    // measure the distance in resource space, where the grid is uniform
                    const float dx = symbol.centre_x - resource_x;
                    const float dy = symbol.centre_y - resource_y;
                    const float distance = std::sqrt(dx * dx + dy * dy) - symbol.half_extent;

                    // a symbol is hit when the pointer is within tolerance of its outline or its body
                    if (distance <= tolerance && distance < best_distance) {
                        best_distance = distance;
                        best.hit = true;
                        best.symbol_index = index;

                        // report the distance in window pixels, because that is the unit the pointer is in
                        best.distance = std::max(0.0F, distance) * m_view.scale;
                    }
                }
            }
        }

        return best;
    }

} // namespace albor::a1
