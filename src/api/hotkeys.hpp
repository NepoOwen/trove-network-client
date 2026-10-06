// File: api/hotkeys.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#pragma once
#include "api_handler.hpp"

namespace api {

// Starts a background thread that polls a few F-keys and fires a canned
// send_net_event() call through api when one is pressed -- a quick way to
// try different events live without rebuilding. Detached; runs until the
// process exits. Each canned call uses api.self_id() at the moment it fires,
// so it always targets whatever entity id was last seen (see hotkeys.cpp).
void start_test_hotkeys(ApiHandler& api);

}
