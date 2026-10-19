#pragma once

#include "phantomcrash/host_controls.hpp"

namespace phantomcrash {
// Native modal editor following the Lodoss settings dialog's layout/lifetime.
bool edit_host_controls(void *owner, HostControls &controls);
} // namespace phantomcrash
