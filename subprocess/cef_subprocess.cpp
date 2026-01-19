// CEF Subprocess Helper
// This is a minimal executable that CEF spawns for renderer/GPU processes.
// It must be separate from the main application to enable multi-process mode
// which is required for GPU compositing and shared textures.

#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/wrapper/cef_message_router.h"

#if defined(_WIN32)
#include <windows.h>
#endif

// Message router config - MUST match the browser process config exactly
CefMessageRouterConfig GetMessageRouterConfig() {
    CefMessageRouterConfig config;
    config.js_query_function = "cefQuery";
    config.js_cancel_function = "cefQueryCancel";
    return config;
}

// CefApp for subprocess - handles message routing on renderer side
class SubprocessApp : public CefApp, public CefRenderProcessHandler {
public:
    SubprocessApp() {
        // Create message router for renderer side - this injects cefQuery into JS
        m_messageRouter = CefMessageRouterRendererSide::Create(GetMessageRouterConfig());
    }
    
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
        return this;
    }
    
    // Called when a new V8 context is created - inject cefQuery function
    void OnContextCreated(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefV8Context> context) override {
        m_messageRouter->OnContextCreated(browser, frame, context);
    }
    
    // Called when a V8 context is released
    void OnContextReleased(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefV8Context> context) override {
        m_messageRouter->OnContextReleased(browser, frame, context);
    }
    
    // Handle messages from browser process
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override {
        return m_messageRouter->OnProcessMessageReceived(browser, frame, source_process, message);
    }

private:
    CefRefPtr<CefMessageRouterRendererSide> m_messageRouter;
    
    IMPLEMENT_REFCOUNTING(SubprocessApp);
    DISALLOW_COPY_AND_ASSIGN(SubprocessApp);
};

#if defined(_WIN32)
int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE hPrevInstance,
                      LPWSTR lpCmdLine,
                      int nCmdShow) {
    CefMainArgs main_args(hInstance);
#else
int main(int argc, char* argv[]) {
    CefMainArgs main_args(argc, argv);
#endif

    // Create the subprocess app
    CefRefPtr<SubprocessApp> app(new SubprocessApp());
    
    // Execute the subprocess logic. This will block until the subprocess exits.
    // Returns -1 if this is the browser process (shouldn't happen for subprocess exe)
    int exit_code = CefExecuteProcess(main_args, app, nullptr);
    
    return exit_code;
}
