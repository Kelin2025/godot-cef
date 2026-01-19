#pragma once

#include "include/cef_client.h"
#include "include/cef_render_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_request_handler.h"
#include "include/wrapper/cef_message_router.h"
#include <functional>
#include <mutex>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef CEF_USE_D3D12_INTEROP
// Forward declare D3D12Interop
namespace CefWebviewGodot { class D3D12Interop; }
#endif

namespace CefWebviewGodot {

// Render handler for offscreen rendering (CPU path - fallback)
class OffscreenRenderHandler : public CefRenderHandler {
public:
    OffscreenRenderHandler(int width, int height);
    
    void Resize(int width, int height);
    
    // Get pixel data (BGRA format)
    const uint8_t* GetPixelData() const { return m_pixelBuffer.data(); }
    size_t GetPixelDataSize() const { return m_pixelBuffer.size(); }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    bool HasNewFrame() const { return m_hasNewFrame; }
    void ClearNewFrameFlag() { m_hasNewFrame = false; }
    
    // Shared texture support (Windows only)
    bool IsSharedTextureEnabled() const { return m_useSharedTexture; }
    void SetSharedTextureEnabled(bool enabled) { m_useSharedTexture = enabled; }
    void* GetSharedHandle() const { return m_sharedHandle; }
    bool HasNewSharedFrame() const { return m_hasNewSharedFrame; }
    void ClearNewSharedFrameFlag() { m_hasNewSharedFrame = false; }
    
#ifdef CEF_USE_D3D12_INTEROP
    // Set the D3D12 interop instance for immediate texture copy in OnAcceleratedPaint
    void SetD3D12Interop(D3D12Interop* interop) { m_d3d12Interop = interop; }
#endif

    // CefRenderHandler methods
    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
    void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                 const RectList& dirtyRects, const void* buffer,
                 int width, int height) override;
    
    // Accelerated paint for shared GPU textures (Windows D3D11)
    void OnAcceleratedPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                            const RectList& dirtyRects, 
                            const CefAcceleratedPaintInfo& info) override;

private:
    int m_width;
    int m_height;
    std::vector<uint8_t> m_pixelBuffer;
    mutable std::mutex m_bufferMutex;
    bool m_hasNewFrame = false;
    
    // Shared texture (GPU path)
    bool m_useSharedTexture = false;
    void* m_sharedHandle = nullptr;
    bool m_hasNewSharedFrame = false;
    
#ifdef CEF_USE_D3D12_INTEROP
    D3D12Interop* m_d3d12Interop = nullptr;
#endif

    IMPLEMENT_REFCOUNTING(OffscreenRenderHandler);
    DISALLOW_COPY_AND_ASSIGN(OffscreenRenderHandler);
};

// Callback type for JS messages from the browser
using JsMessageCallback = std::function<std::string(const std::string& message)>;

// Callback type for load finished events
using LoadFinishedCallback = std::function<void()>;

// Message handler for cefQuery calls from JavaScript
class GodotMessageHandler : public CefMessageRouterBrowserSide::Handler {
public:
    explicit GodotMessageHandler(JsMessageCallback callback) : m_callback(callback) {}
    
    bool OnQuery(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int64_t query_id,
                 const CefString& request,
                 bool persistent,
                 CefRefPtr<Callback> callback) override {
        if (m_callback) {
            std::string response = m_callback(request.ToString());
            callback->Success(response);
            return true;
        }
        callback->Failure(0, "No handler registered");
        return true;
    }
    
    void OnQueryCanceled(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         int64_t query_id) override {}

private:
    JsMessageCallback m_callback;
};

// Browser client - handles browser events
class GodotCefClient : public CefClient,
                       public CefLifeSpanHandler,
                       public CefLoadHandler,
                       public CefRequestHandler {
public:
    explicit GodotCefClient(CefRefPtr<OffscreenRenderHandler> renderHandler);
    ~GodotCefClient();

    // Accessors
    CefRefPtr<CefBrowser> GetBrowser() const { return m_browser; }
    CefRefPtr<OffscreenRenderHandler> GetOffscreenRenderHandler() const { 
        return m_renderHandler; 
    }
    
    // Set callback for JS messages
    void SetJsMessageCallback(JsMessageCallback callback);
    
    // Set callback for load finished events
    void SetLoadFinishedCallback(LoadFinishedCallback callback);

    // CefClient methods
    CefRefPtr<CefRenderHandler> GetRenderHandler() override {
        return m_renderHandler;
    }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
        return this;
    }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override {
        return this;
    }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override {
        return this;
    }
    
    // Process message from renderer (for message router)
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override;

    // CefLifeSpanHandler methods
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    // CefLoadHandler methods
    void OnLoadStart(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     TransitionType transition_type) override;
    void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   int httpStatusCode) override;
    void OnLoadError(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     ErrorCode errorCode,
                     const CefString& errorText,
                     const CefString& failedUrl) override;
                     
    // CefRequestHandler methods
    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefRequest> request,
                        bool user_gesture,
                        bool is_redirect) override;

private:
    CefRefPtr<CefBrowser> m_browser;
    CefRefPtr<OffscreenRenderHandler> m_renderHandler;
    CefRefPtr<CefMessageRouterBrowserSide> m_messageRouter;
    GodotMessageHandler* m_messageHandler = nullptr;
    LoadFinishedCallback m_loadFinishedCallback;

    IMPLEMENT_REFCOUNTING(GodotCefClient);
    DISALLOW_COPY_AND_ASSIGN(GodotCefClient);
};

} // namespace CefWebviewGodot
