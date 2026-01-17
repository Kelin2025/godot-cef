#pragma once

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <memory>
#include <mutex>
#include <vector>

namespace CefWebviewGodot {

class GodotCefClient;
class OffscreenRenderHandler;

class CefWebviewNode : public godot::Control {
    GDCLASS(CefWebviewNode, godot::Control)

public:
    CefWebviewNode();
    ~CefWebviewNode();

    // Godot lifecycle
    void _ready() override;
    void _process(double delta) override;
    void _draw() override;
    void _gui_input(const godot::Ref<godot::InputEvent>& event) override;

    // API
    void load_url(const godot::String& url);
    void load_html(const godot::String& html, const godot::String& base_url = "");
    godot::String get_url() const;
    void execute_javascript(const godot::String& script);
    
    // Properties
    void set_url(const godot::String& url);
    godot::String get_initial_url() const;
    
    void set_transparent(bool transparent);
    bool get_transparent() const;
    
    void set_capture_keyboard(bool capture);
    bool get_capture_keyboard() const;
    
    // Status
    bool is_gpu_accelerated() const { return m_useGpuPath; }
    godot::String get_status() const;
    
    // JS message handling - called internally when JS sends a message
    void _handle_js_message(const godot::String& message, const godot::String& response);
    
    // Queue for pending JS messages (thread-safe communication)
    struct PendingJsMessage {
        godot::String message;
        bool processed = false;
        godot::String response;
    };

protected:
    static void _bind_methods();

private:
    void createBrowser();
    void setupGpuTexture();
    void updateTexture();
    void forwardMouseEvent(const godot::Ref<godot::InputEvent>& event);
    void forwardKeyEvent(const godot::Ref<godot::InputEvent>& event);
    godot::String resolve_url(const godot::String& url) const;

    // CEF client - stored as opaque pointer to avoid CEF header conflicts
    void* m_clientPtr = nullptr;
    
    // Texture display
    godot::Ref<godot::ImageTexture> m_cpuTexture;
    godot::Ref<godot::Texture2DRD> m_gpuTexture;
    godot::RID m_rdTextureRid;
    
    // State
    bool m_initialized = false;
    bool m_useGpuPath = false;
    bool m_transparent = false;
    godot::String m_initialUrl = "about:blank";
    godot::Vector2 m_previousSize;
    double m_timeSinceUpdate = 0.0;
    
    // Mouse button state tracking (to filter duplicate events)
    bool m_leftButtonDown = false;
    bool m_rightButtonDown = false;
    bool m_middleButtonDown = false;
    
    // Input capture settings
    bool m_captureKeyboard = false;  // When false, keys pass through to game
    
    // JS message queue
    std::vector<PendingJsMessage> m_pendingMessages;
    std::mutex m_messageMutex;
    
    // Static CEF init flag
    static bool s_cefInitialized;
};

} // namespace CefWebviewGodot
