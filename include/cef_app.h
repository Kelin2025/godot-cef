#pragma once

#include "include/cef_app.h"
#include "include/cef_command_line.h"

namespace CefWebviewGodot {

// CEF Application - handles process-level callbacks
class GodotCefApp : public CefApp, public CefBrowserProcessHandler {
public:
    GodotCefApp() = default;

    // CefApp methods
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
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
