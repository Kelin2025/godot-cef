#include "cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/wrapper/cef_helpers.h"

#include <godot_cpp/variant/utility_functions.hpp>

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
    if (g_cefInitialized) return true;
    
    godot::UtilityFunctions::print("[CEF] Initializing CEF...");
    
    // Get the executable path for subprocess
    CefMainArgs main_args;
    
    g_app = new GodotCefApp();
    
    CefSettings settings;
    settings.no_sandbox = true;
    settings.windowless_rendering_enabled = true;
    settings.multi_threaded_message_loop = false;  // We'll pump messages ourselves
    settings.external_message_pump = true;
    
    // Set log file
    CefString(&settings.log_file).FromASCII("cef_debug.log");
    settings.log_severity = LOGSEVERITY_INFO;
    
    // Initialize CEF
    if (!CefInitialize(main_args, settings, g_app.get(), nullptr)) {
        godot::UtilityFunctions::print("[CEF] ERROR: Failed to initialize CEF");
        return false;
    }
    
    g_cefInitialized = true;
    godot::UtilityFunctions::print("[CEF] CEF initialized successfully");
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
