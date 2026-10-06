// File: app/session.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#pragma once

namespace app {

// Parses argv and runs one of: --selftest, --login <email> <password> [code],
// or a normal game session for a region ("EU" | "NA" | "PTS" | "host:port").
int run(int argc, char** argv);

}
