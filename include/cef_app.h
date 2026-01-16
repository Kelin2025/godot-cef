#pragma once

#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_render_process_handler.h"
#include "include/wrapper/cef_message_router.h"

namespace CefWebviewGodot {

// Render process handler for message router (needed even in single-process mode)
class GodotRenderProcessHandler : public CefRenderProcessHandler {
public:
    GodotRenderProcessHandler();
    
    void OnContextCreated(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefV8Context> context) override;
    void OnContextReleased(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefV8Context> context) override;
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override;

private:
    CefRefPtr<CefMessageRouterRendererSide> m_messageRouter;
    
    IMPLEMENT_REFCOUNTING(GodotRenderProcessHandler);
    DISALLOW_COPY_AND_ASSIGN(GodotRenderProcessHandler);
};

// CEF Application - handles process-level callbacks
class GodotCefApp : public CefApp, 
                    public CefBrowserProcessHandler {
public:
    GodotCefApp();

    // CefApp methods
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
    }
    
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
        return m_renderProcessHandler;
    }
    
    // Add single-process flag to prevent CEF from spawning multiple Godot instances
    void OnBeforeCommandLineProcessing(
        const CefString& process_type,
        CefRefPtr<CefCommandLine> command_line) override {
        // Single-process mode: all CEF code runs in the main process
        // This prevents CEF from launching Godot.exe as subprocess
        command_line->AppendSwitch("single-process");
        // Also disable GPU process to reduce complexity
        command_line->AppendSwitch("disable-gpu-compositing");
    }

    // CefBrowserProcessHandler methods
    void OnContextInitialized() override;

private:
    CefRefPtr<GodotRenderProcessHandler> m_renderProcessHandler;
    
    IMPLEMENT_REFCOUNTING(GodotCefApp);
    DISALLOW_COPY_AND_ASSIGN(GodotCefApp);
};

// Initialize CEF - call once at startup
bool InitializeCef();

// Shutdown CEF - call at exit
void ShutdownCef();

// Process CEF message loop - call each frame
void UpdateCef();

} // namespace CefWebviewGodot
