#pragma once

#include "include/cef_client.h"
#include "include/cef_render_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include <functional>
#include <mutex>
#include <vector>

namespace CefWebviewGodot {

// Render handler for offscreen rendering
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

    // CefRenderHandler methods
    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
    void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                 const RectList& dirtyRects, const void* buffer,
                 int width, int height) override;

private:
    int m_width;
    int m_height;
    std::vector<uint8_t> m_pixelBuffer;
    mutable std::mutex m_bufferMutex;
    bool m_hasNewFrame = false;

    IMPLEMENT_REFCOUNTING(OffscreenRenderHandler);
    DISALLOW_COPY_AND_ASSIGN(OffscreenRenderHandler);
};

// Browser client - handles browser events
class GodotCefClient : public CefClient,
                       public CefLifeSpanHandler,
                       public CefLoadHandler {
public:
    explicit GodotCefClient(CefRefPtr<OffscreenRenderHandler> renderHandler);

    // Accessors
    CefRefPtr<CefBrowser> GetBrowser() const { return m_browser; }
    CefRefPtr<OffscreenRenderHandler> GetOffscreenRenderHandler() const { 
        return m_renderHandler; 
    }

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

private:
    CefRefPtr<CefBrowser> m_browser;
    CefRefPtr<OffscreenRenderHandler> m_renderHandler;

    IMPLEMENT_REFCOUNTING(GodotCefClient);
    DISALLOW_COPY_AND_ASSIGN(GodotCefClient);
};

} // namespace CefWebviewGodot
