// File: app/selftest.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#pragma once

namespace app {

// Crypto + deflate round-trip sanity checks. Prints results, returns true
// if both passed.
bool run_selftests();

}
