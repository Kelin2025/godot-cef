#include "cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/wrapper/cef_helpers.h"

#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <string>
#include <filesystem>
#include <fstream>

namespace CefWebviewGodot {

// Forward declaration of message router config from cef_client.cpp
CefMessageRouterConfig GetMessageRouterConfig();

static CefRefPtr<GodotCefApp> g_app;
static bool g_cefInitialized = false;

// GodotRenderProcessHandler implementation
GodotRenderProcessHandler::GodotRenderProcessHandler() {
    m_messageRouter = CefMessageRouterRendererSide::Create(GetMessageRouterConfig());
}

void GodotRenderProcessHandler::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                                  CefRefPtr<CefFrame> frame,
                                                  CefRefPtr<CefV8Context> context) {
    m_messageRouter->OnContextCreated(browser, frame, context);
}

void GodotRenderProcessHandler::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                                   CefRefPtr<CefFrame> frame,
                                                   CefRefPtr<CefV8Context> context) {
    m_messageRouter->OnContextReleased(browser, frame, context);
}

bool GodotRenderProcessHandler::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                                          CefRefPtr<CefFrame> frame,
                                                          CefProcessId source_process,
                                                          CefRefPtr<CefProcessMessage> message) {
    return m_messageRouter->OnProcessMessageReceived(browser, frame, source_process, message);
}

// GodotCefApp implementation
GodotCefApp::GodotCefApp() {
    m_renderProcessHandler = new GodotRenderProcessHandler();
}

void GodotCefApp::OnContextInitialized() {
    CEF_REQUIRE_UI_THREAD();
    godot::UtilityFunctions::print("[CEF] Context initialized");
}

bool InitializeCef() {
    if (g_cefInitialized) {
        godot::UtilityFunctions::print("[CEF] Already initialized, skipping");
        return true;
    }
    
    godot::UtilityFunctions::print("[CEF] Initializing CEF...");
    
    // Get the executable path for subprocess
    CefMainArgs main_args;
    godot::UtilityFunctions::print("[CEF] Created main_args");
    
    g_app = new GodotCefApp();
    godot::UtilityFunctions::print("[CEF] Created GodotCefApp");
    
    CefSettings settings;
    settings.no_sandbox = true;
    settings.windowless_rendering_enabled = true;
    settings.multi_threaded_message_loop = false;  // We'll pump messages ourselves
    settings.external_message_pump = false;  // Don't use external pump
    
    // Use Godot's user:// directory for cache
    godot::String userDir = godot::OS::get_singleton()->get_user_data_dir();
    std::string cachePath = std::string(userDir.utf8().get_data()) + "/cef_cache";
    std::string dataPath = cachePath + "/data";
    CefString(&settings.root_cache_path).FromASCII(cachePath.c_str());
    CefString(&settings.cache_path).FromASCII(dataPath.c_str());
    
    godot::UtilityFunctions::print("[CEF] Cache path: ", godot::String(cachePath.c_str()));
    godot::UtilityFunctions::print("[CEF] Data path: ", godot::String(dataPath.c_str()));
    
    // Check if lockfile exists and try to remove stale ones
    std::string lockfilePath = cachePath + "/lockfile";
    if (std::filesystem::exists(lockfilePath)) {
        godot::UtilityFunctions::print("[CEF] WARNING: Lockfile exists at: ", godot::String(lockfilePath.c_str()));
        godot::UtilityFunctions::print("[CEF] Attempting to remove stale lockfile...");
        std::error_code ec;
        if (std::filesystem::remove(lockfilePath, ec)) {
            godot::UtilityFunctions::print("[CEF] Successfully removed stale lockfile");
        } else {
            godot::UtilityFunctions::print("[CEF] Could not remove lockfile: ", godot::String(ec.message().c_str()));
            godot::UtilityFunctions::print("[CEF] Another CEF instance may be running");
        }
    } else {
        godot::UtilityFunctions::print("[CEF] No lockfile found (good)");
    }
    
    // Check if cache directory exists
    if (std::filesystem::exists(cachePath)) {
        godot::UtilityFunctions::print("[CEF] Cache directory exists");
        // List contents
        for (const auto& entry : std::filesystem::directory_iterator(cachePath)) {
            godot::UtilityFunctions::print("[CEF]   - ", godot::String(entry.path().filename().string().c_str()));
        }
    } else {
        godot::UtilityFunctions::print("[CEF] Cache directory does not exist, will be created");
    }
    
    // Set log file
    CefString(&settings.log_file).FromASCII("cef_debug.log");
    settings.log_severity = LOGSEVERITY_VERBOSE;  // Maximum verbosity
    
    godot::UtilityFunctions::print("[CEF] Settings configured:");
    godot::UtilityFunctions::print("[CEF]   no_sandbox: ", settings.no_sandbox);
    godot::UtilityFunctions::print("[CEF]   windowless_rendering_enabled: ", settings.windowless_rendering_enabled);
    godot::UtilityFunctions::print("[CEF]   multi_threaded_message_loop: ", settings.multi_threaded_message_loop);
    godot::UtilityFunctions::print("[CEF]   external_message_pump: ", settings.external_message_pump);
    godot::UtilityFunctions::print("[CEF]   log_severity: VERBOSE");
    
    godot::UtilityFunctions::print("[CEF] Calling CefInitialize...");
    
    // Initialize CEF
    bool result = CefInitialize(main_args, settings, g_app.get(), nullptr);
    
    if (!result) {
        godot::UtilityFunctions::print("[CEF] ERROR: CefInitialize returned false");
        godot::UtilityFunctions::print("[CEF] Possible causes:");
        godot::UtilityFunctions::print("[CEF]   1. Another CEF instance is running (check lockfile)");
        godot::UtilityFunctions::print("[CEF]   2. CEF binaries missing from Godot directory");
        godot::UtilityFunctions::print("[CEF]   3. Incompatible CEF version");
        godot::UtilityFunctions::print("[CEF]   4. Check cef_debug.log for more details");
        g_app = nullptr;
        return false;
    }
    
    g_cefInitialized = true;
    godot::UtilityFunctions::print("[CEF] CEF initialized successfully!");
    return true;
}

void ShutdownCef() {
    if (!g_cefInitialized) return;
    
    godot::UtilityFunctions::print("[CEF] Shutting down CEF...");
    CefShutdown();
    g_app = nullptr;
    g_cefInitialized = false;
}

void UpdateCef() {
    if (!g_cefInitialized) return;
    CefDoMessageLoopWork();
}

} // namespace CefWebviewGodot
