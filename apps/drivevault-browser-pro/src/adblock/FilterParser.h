#pragma once

#include "NetRule.h"

namespace dv::br::ab {

// One line of a filter list. Network filters are returned in `rule`; cosmetic filters (## #@# +js(...)) are only classified here (applied in Phase 3);
// options this program cannot honour (removeparam, csp, rewrite, popup, regular expressions ...) make the line Unsupported - it is never half-applied.
ParseKind parseLine(const QString& line, NetRule& rule);

} // namespace dv::br::ab
