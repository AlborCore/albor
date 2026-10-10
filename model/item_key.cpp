#include "model/item_key.h"

namespace albor::model
{
    ItemKey ItemKey::from_int(long long value) {
        // the number is written as text, because a key is compared as text (§18.1, AD-13)
        return ItemKey(std::to_string(value));
    }
} // namespace albor::model
