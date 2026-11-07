#pragma once

namespace dv::crash {

// Installs a process-wide handler that writes the exception code / address (no personal data,
// no file paths, no memory contents) to the application log before the process terminates.
void install();

} // namespace dv::crash
