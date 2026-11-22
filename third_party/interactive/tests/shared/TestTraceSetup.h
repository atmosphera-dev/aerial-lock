// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// Shared test-only trace activation. Reads IA_TEST_TRACE_MODE=summary|trace and
// IA_TEST_TRACE_DIR=<fresh directory>, activates the real recorder around the
// test run, and exports per-process uniquely named evidence at teardown. Normal
// application runs are unaffected: the environment variables are only honored in
// binaries that call TestTrace::install() explicitly.
namespace InterActive::TestTrace {

// Call once at the top of a test main(), before any Appearance is constructed.
// argv0 names the evidence files (applicationFilePath is empty before the app
// object exists). Returns true when a trace session was activated.
bool install(const char *argv0);

// Assert activation and nonzero expected records at teardown; call after the
// test framework returns. Exports into IA_TEST_TRACE_DIR. Returns false when a
// requested session produced no records or could not export.
bool finalize();

} // namespace InterActive::TestTrace
