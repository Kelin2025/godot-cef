#include "cef_client.h"

#include <godot_cpp/variant/utility_functions.hpp>
#include <cstring>

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
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    
    if (width != m_width || height != m_height) {
        // Size mismatch, skip this frame
        return;
    }
    
    // Copy pixel data (CEF provides BGRA)
    std::memcpy(m_pixelBuffer.data(), buffer, m_pixelBuffer.size());
    m_hasNewFrame = true;
}

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
    godot::UtilityFunctions::print("[CEF] Browser created");
}

void GodotCefClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    m_messageRouter->OnBeforeClose(browser);
    m_browser = nullptr;
    godot::UtilityFunctions::print("[CEF] Browser closed");
}

void GodotCefClient::OnLoadStart(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  TransitionType transition_type) {
    if (frame->IsMain()) {
        godot::UtilityFunctions::print("[CEF] Load started: ", 
            frame->GetURL().ToString().c_str());
    }
}

void GodotCefClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                int httpStatusCode) {
    if (frame->IsMain()) {
        godot::UtilityFunctions::print("[CEF] Load finished: status=", httpStatusCode);
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
        godot::UtilityFunctions::print("[CEF] Load error: ", 
            errorText.ToString().c_str(), " URL: ", failedUrl.ToString().c_str());
    }
}

} // namespace CefWebviewGodot
