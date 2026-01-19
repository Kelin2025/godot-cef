#include "cef_app.h"
#include "cef_debug.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/wrapper/cef_helpers.h"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <string>
#include <filesystem>
#include <fstream>

namespace CefWebviewGodot {

// Forward declaration of message router config from cef_client.cpp
CefMessageRouterConfig GetMessageRouterConfig();

static CefRefPtr<GodotCefApp> g_app;
static bool g_cefInitialized = false;

// Initialize static member
bool GodotCefApp::s_useMultiProcess = false;

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
    CEF_DEBUG_PRINT("[CEF] Context initialized");
}

bool InitializeCef() {
    if (g_cefInitialized) {
        CEF_DEBUG_PRINT("[CEF] Already initialized, skipping");
        return true;
    }
    
    CEF_DEBUG_PRINT("[CEF] Initializing CEF...");
    
    // Get the executable path for subprocess
    CefMainArgs main_args;
    CEF_DEBUG_PRINT("[CEF] Created main_args");
    
    g_app = new GodotCefApp();
    CEF_DEBUG_PRINT("[CEF] Created GodotCefApp");
    
    CefSettings settings;
    settings.no_sandbox = true;
    settings.windowless_rendering_enabled = true;
    settings.multi_threaded_message_loop = false;  // We'll pump messages ourselves
    settings.external_message_pump = false;  // Don't use external pump
    
    // Set subprocess path - this enables multi-process mode with GPU support
    // Try multiple locations: project directory first, then executable directory
    std::string subprocessPath;
    bool foundSubprocess = false;
    
    // Try 1: Use res:// path (project directory) - works in editor
    godot::String resPath = godot::ProjectSettings::get_singleton()->globalize_path("res://addons/cef_webview/bin/windows/cef_subprocess.exe");
    std::string projectSubprocess = std::string(resPath.utf8().get_data());
    if (std::filesystem::exists(projectSubprocess)) {
        subprocessPath = projectSubprocess;
        foundSubprocess = true;
        CEF_DEBUG_PRINT("[CEF] Subprocess found in project: ", godot::String(subprocessPath.c_str()));
    }
    
    // Try 2: Next to executable (for exported builds)
    if (!foundSubprocess) {
        godot::String exePath = godot::OS::get_singleton()->get_executable_path();
        std::string exeDir = std::string(exePath.utf8().get_data());
        size_t lastSlash = exeDir.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            exeDir = exeDir.substr(0, lastSlash);
        }
        std::string exeSubprocess = exeDir + "/cef_subprocess.exe";
        if (std::filesystem::exists(exeSubprocess)) {
            subprocessPath = exeSubprocess;
            foundSubprocess = true;
            CEF_DEBUG_PRINT("[CEF] Subprocess found next to exe: ", godot::String(subprocessPath.c_str()));
        }
    }
    
    if (foundSubprocess) {
        CefString(&settings.browser_subprocess_path).FromASCII(subprocessPath.c_str());
        GodotCefApp::s_useMultiProcess = true;
        CEF_DEBUG_PRINT("[CEF] Multi-process mode enabled (GPU shared textures available)");
    } else {
        CEF_DEBUG_PRINT("[CEF] WARNING: Subprocess not found!");
        CEF_DEBUG_PRINT("[CEF] Tried: ", godot::String(projectSubprocess.c_str()));
        CEF_DEBUG_PRINT("[CEF] Falling back to single-process mode (no GPU shared textures)");
        GodotCefApp::s_useMultiProcess = false;
    }
    
    // Use Godot's user:// directory for cache
    godot::String userDir = godot::OS::get_singleton()->get_user_data_dir();
    std::string cachePath = std::string(userDir.utf8().get_data()) + "/cef_cache";
    std::string dataPath = cachePath + "/data";
    CefString(&settings.root_cache_path).FromASCII(cachePath.c_str());
    CefString(&settings.cache_path).FromASCII(dataPath.c_str());
    
    CEF_DEBUG_PRINT("[CEF] Cache path: ", godot::String(cachePath.c_str()));
    CEF_DEBUG_PRINT("[CEF] Data path: ", godot::String(dataPath.c_str()));
    
    // Check if lockfile exists and try to remove stale ones
    std::string lockfilePath = cachePath + "/lockfile";
    if (std::filesystem::exists(lockfilePath)) {
        CEF_DEBUG_PRINT("[CEF] WARNING: Lockfile exists at: ", godot::String(lockfilePath.c_str()));
        CEF_DEBUG_PRINT("[CEF] Attempting to remove stale lockfile...");
        std::error_code ec;
        if (std::filesystem::remove(lockfilePath, ec)) {
            CEF_DEBUG_PRINT("[CEF] Successfully removed stale lockfile");
        } else {
            CEF_DEBUG_PRINT("[CEF] Could not remove lockfile: ", godot::String(ec.message().c_str()));
            CEF_DEBUG_PRINT("[CEF] Another CEF instance may be running");
        }
    } else {
        CEF_DEBUG_PRINT("[CEF] No lockfile found (good)");
    }
    
    // Check if cache directory exists
    if (std::filesystem::exists(cachePath)) {
        CEF_DEBUG_PRINT("[CEF] Cache directory exists");
        // List contents
        for (const auto& entry : std::filesystem::directory_iterator(cachePath)) {
            CEF_DEBUG_PRINT("[CEF]   - ", godot::String(entry.path().filename().string().c_str()));
        }
    } else {
        CEF_DEBUG_PRINT("[CEF] Cache directory does not exist, will be created");
    }
    
    // Set log file - temporarily force verbose to debug GPU issues
    std::string logPath = cachePath + "/cef_debug.log";
    CefString(&settings.log_file).FromASCII(logPath.c_str());
    // TEMPORARY: Force verbose logging to debug shared texture issues
    settings.log_severity = LOGSEVERITY_VERBOSE;
    bool debugLogging = true;
    CEF_DEBUG_PRINT("[CEF] Log file: ", godot::String(logPath.c_str()));
    
    CEF_DEBUG_PRINT("[CEF] Settings configured:");
    CEF_DEBUG_PRINT("[CEF]   no_sandbox: ", settings.no_sandbox);
    CEF_DEBUG_PRINT("[CEF]   windowless_rendering_enabled: ", settings.windowless_rendering_enabled);
    CEF_DEBUG_PRINT("[CEF]   multi_threaded_message_loop: ", settings.multi_threaded_message_loop);
    CEF_DEBUG_PRINT("[CEF]   external_message_pump: ", settings.external_message_pump);
    CEF_DEBUG_PRINT("[CEF]   log_severity: ", debugLogging ? "VERBOSE" : "DISABLED");
    
    CEF_DEBUG_PRINT("[CEF] Calling CefInitialize...");
    
    // Initialize CEF
    bool result = CefInitialize(main_args, settings, g_app.get(), nullptr);
    
    if (!result) {
        CEF_DEBUG_PRINT("[CEF] ERROR: CefInitialize returned false");
        CEF_DEBUG_PRINT("[CEF] Possible causes:");
        CEF_DEBUG_PRINT("[CEF]   1. Another CEF instance is running (check lockfile)");
        CEF_DEBUG_PRINT("[CEF]   2. CEF binaries missing from Godot directory");
        CEF_DEBUG_PRINT("[CEF]   3. Incompatible CEF version");
        CEF_DEBUG_PRINT("[CEF]   4. Check cef_debug.log for more details");
        g_app = nullptr;
        return false;
    }
    
    g_cefInitialized = true;
    CEF_DEBUG_PRINT("[CEF] CEF initialized successfully!");
    return true;
}

void ShutdownCef() {
    if (!g_cefInitialized) return;
    
    CEF_DEBUG_PRINT("[CEF] Shutting down CEF...");
    CefShutdown();
    g_app = nullptr;
    g_cefInitialized = false;
}

void UpdateCef() {
    if (!g_cefInitialized) return;
    CefDoMessageLoopWork();
}

} // namespace CefWebviewGodot
