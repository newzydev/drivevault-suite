#pragma once

#include "Cosmetic.h"

namespace dv::br::ab {

// The one script that goes into a page before the page's own scripts run (DocumentCreation, main world): the library `libraryJs` (resources/adblock-page.js, it
// reads a variable CFG) with the configuration of this page in front. Empty when there is nothing to do for the page.
//   sel   specific hide selectors        css   injected style rules          sl    [[scriptlet, [args]]...]
//   goff  generic names switched off     gen   generic hiding on or off      cls / ids  the names of the generic rules (space separated)
QString buildPageScript(const QString& libraryJs, const PageCosmetics& page, const QString& genericClasses, const QString& genericIds);

} // namespace dv::br::ab
