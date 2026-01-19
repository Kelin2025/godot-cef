#pragma once

#include <godot_cpp/variant/utility_functions.hpp>

namespace CefWebviewGodot {

// Global debug flag for CEF logging
// Set via CefWebviewNode::set_debug_logging(true) before CEF initializes
inline bool& GetDebugLoggingFlag() {
    static bool s_debugLogging = false;
    return s_debugLogging;
}

inline void SetDebugLogging(bool enabled) {
    GetDebugLoggingFlag() = enabled;
}

inline bool IsDebugLogging() {
    return GetDebugLoggingFlag();
}

// Helper macro for conditional debug printing
#define CEF_DEBUG_PRINT(...) \
    do { if (CefWebviewGodot::IsDebugLogging()) godot::UtilityFunctions::print(__VA_ARGS__); } while(0)

// Always print (for errors)
#define CEF_ERROR_PRINT(...) \
    do { godot::UtilityFunctions::print(__VA_ARGS__); } while(0)

} // namespace CefWebviewGodot
