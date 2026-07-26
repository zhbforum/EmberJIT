#pragma once

// Keep platform selection in one place so production code and tests expose
// the same native-execution contract.
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
#define EMBER_HAS_WIN64_JIT 1
#else
#define EMBER_HAS_WIN64_JIT 0
#endif
