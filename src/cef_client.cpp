#include "cef_client.h"
#include "cef_debug.h"

#include <cstring>

#ifdef CEF_USE_D3D12_INTEROP
#include "d3d12_interop.h"
#endif

namespace CefWebviewGodot {

// Message router config
CefMessageRouterConfig GetMessageRouterConfig() {
    CefMessageRouterConfig config;
    config.js_query_function = "cefQuery";
    config.js_cancel_function = "cefQueryCancel";
    return config;
}

// OffscreenRenderHandler implementation
OffscreenRenderHandler::OffscreenRenderHandler(int width, int height)
    : m_width(width)
    , m_height(height)
{
    m_pixelBuffer.resize(width * height * 4);  // BGRA
}

void OffscreenRenderHandler::Resize(int width, int height) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_width = width;
    m_height = height;
    m_pixelBuffer.resize(width * height * 4);
}

void OffscreenRenderHandler::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) {
    rect = CefRect(0, 0, m_width, m_height);
}

void OffscreenRenderHandler::OnPaint(CefRefPtr<CefBrowser> browser,
                                      PaintElementType type,
                                      const RectList& dirtyRects,
                                      const void* buffer,
                                      int width, int height) {
    // Log to see which paint path is being used
    static int paintCount = 0;
    if (++paintCount <= 5 || paintCount % 120 == 0) {
        CEF_DEBUG_PRINT("[CEF] OnPaint (CPU) called! useSharedTexture=", 
            m_useSharedTexture ? "true" : "false", " count=", paintCount);
    }
    
    // Skip if using shared texture path
    if (m_useSharedTexture) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    
    if (width != m_width || height != m_height) {
        // Size mismatch, skip this frame
        return;
    }
    
    // Copy only dirty rectangles instead of entire buffer (CEF provides BGRA)
    const char* srcBuffer = static_cast<const char*>(buffer);
    const int bytesPerPixel = 4;
    const int rowStride = width * bytesPerPixel;
    
    for (const auto& rect : dirtyRects) {
        for (int y = rect.y; y < rect.y + rect.height; y++) {
            int offset = y * rowStride + rect.x * bytesPerPixel;
            int copySize = rect.width * bytesPerPixel;
            std::memcpy(m_pixelBuffer.data() + offset, srcBuffer + offset, copySize);
        }
    }
    m_hasNewFrame = true;
}

void OffscreenRenderHandler::OnAcceleratedPaint(CefRefPtr<CefBrowser> browser,
                                                 PaintElementType type,
                                                 const RectList& dirtyRects,
                                                 const CefAcceleratedPaintInfo& info) {
    // CRITICAL: The handle is only valid during this callback!
    // We must copy the texture immediately, not defer to _process()
    static int callCount = 0;
    if (++callCount <= 5 || callCount % 60 == 0) {
        CEF_DEBUG_PRINT("[CEF] OnAcceleratedPaint called! handle=", 
            info.shared_texture_handle != nullptr ? "valid" : "null", " count=", callCount);
    }
    
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    
#ifdef CEF_USE_D3D12_INTEROP
    // If D3D12 interop is set, copy the texture immediately
    if (m_d3d12Interop && info.shared_texture_handle) {
        // Copy texture synchronously while handle is still valid
        bool success = m_d3d12Interop->ImportSharedTexture(
            info.shared_texture_handle,
            m_width,
            m_height
        );
        if (success) {
            m_hasNewSharedFrame = true;
        }
        // Don't store handle - it's invalid after this callback returns
        return;
    }
#endif
    
    // Fallback: store handle (may not work if deferred processing)
    m_sharedHandle = info.shared_texture_handle;
    m_hasNewSharedFrame = true;
}

// Add logging to OnPaint to see if CPU path is being used instead


// GodotCefClient implementation
GodotCefClient::GodotCefClient(CefRefPtr<OffscreenRenderHandler> renderHandler)
    : m_renderHandler(renderHandler)
{
    // Create message router
    m_messageRouter = CefMessageRouterBrowserSide::Create(GetMessageRouterConfig());
}

GodotCefClient::~GodotCefClient() {
    if (m_messageHandler) {
        m_messageRouter->RemoveHandler(m_messageHandler);
        delete m_messageHandler;
        m_messageHandler = nullptr;
    }
}

void GodotCefClient::SetJsMessageCallback(JsMessageCallback callback) {
    if (m_messageHandler) {
        m_messageRouter->RemoveHandler(m_messageHandler);
        delete m_messageHandler;
    }
    m_messageHandler = new GodotMessageHandler(callback);
    m_messageRouter->AddHandler(m_messageHandler, false);
}

void GodotCefClient::SetLoadFinishedCallback(LoadFinishedCallback callback) {
    m_loadFinishedCallback = callback;
}

bool GodotCefClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                               CefRefPtr<CefFrame> frame,
                                               CefProcessId source_process,
                                               CefRefPtr<CefProcessMessage> message) {
    return m_messageRouter->OnProcessMessageReceived(browser, frame, source_process, message);
}

bool GodotCefClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefFrame> frame,
                                     CefRefPtr<CefRequest> request,
                                     bool user_gesture,
                                     bool is_redirect) {
    m_messageRouter->OnBeforeBrowse(browser, frame);
    return false;  // Allow navigation
}

void GodotCefClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    m_browser = browser;
    CEF_DEBUG_PRINT("[CEF] Browser created");
}

void GodotCefClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    m_messageRouter->OnBeforeClose(browser);
    m_browser = nullptr;
    CEF_DEBUG_PRINT("[CEF] Browser closed");
}

void GodotCefClient::OnLoadStart(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  TransitionType transition_type) {
    if (frame->IsMain()) {
        CEF_DEBUG_PRINT("[CEF] Load started: ", 
            frame->GetURL().ToString().c_str());
    }
}

void GodotCefClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                int httpStatusCode) {
    if (frame->IsMain()) {
        CEF_DEBUG_PRINT("[CEF] Load finished: status=", httpStatusCode);
        if (m_loadFinishedCallback) {
            m_loadFinishedCallback();
        }
    }
}

void GodotCefClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  ErrorCode errorCode,
                                  const CefString& errorText,
                                  const CefString& failedUrl) {
    if (frame->IsMain()) {
        CEF_DEBUG_PRINT("[CEF] Load error: ", 
            errorText.ToString().c_str(), " URL: ", failedUrl.ToString().c_str());
    }
}

} // namespace CefWebviewGodot
