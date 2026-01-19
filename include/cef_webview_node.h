#pragma once

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <memory>
#include <mutex>
#include <vector>

#ifdef CEF_USE_VULKAN_INTEROP
#include "vulkan_interop.h"
#endif

#ifdef CEF_USE_D3D12_INTEROP
#include "d3d12_interop.h"
#endif

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
    void _input(const godot::Ref<godot::InputEvent>& event) override;
    void _gui_input(const godot::Ref<godot::InputEvent>& event) override;
    void _unhandled_input(const godot::Ref<godot::InputEvent>& event) override;
    void _notification(int what);

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
    
    void set_handle_mouse(bool handle);
    bool get_handle_mouse() const;
    
    void set_handle_keys(bool handle);
    bool get_handle_keys() const;
    
    void set_frame_rate(int fps);
    int get_frame_rate() const;
    
    void set_paused(bool paused);
    bool get_paused() const;
    
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
    
    // Time-based debouncing to prevent CEF feedback loops
    uint64_t m_lastClickTime = 0;
    static constexpr uint64_t CLICK_DEBOUNCE_MS = 16;  // ~1 frame at 60fps
    
    // Input capture settings
    bool m_captureKeyboard = false;  // When false, keys pass through to game
    bool m_handleMouse = true;       // When true, forward mouse events to CEF
    bool m_handleKeys = false;       // When true, forward key events to CEF (default false)
    
    // Rendering settings
    int m_frameRate = 60;            // CEF windowless frame rate
    bool m_paused = false;           // When true, skip all processing (for scene transitions)
    
    // JS message queue
    std::vector<PendingJsMessage> m_pendingMessages;
    std::mutex m_messageMutex;
    
    // Reusable byte array for texture updates (avoids allocation every frame)
    godot::PackedByteArray m_textureBytes;
    
#ifdef CEF_USE_VULKAN_INTEROP
    // Vulkan interop for GPU shared textures
    std::unique_ptr<VulkanInterop> m_vulkanInterop;
    int m_vulkanImportFailCount = 0;
#endif

#ifdef CEF_USE_D3D12_INTEROP
    // D3D12 interop for GPU shared textures (when Godot runs D3D12 backend)
    std::unique_ptr<D3D12Interop> m_d3d12Interop;
    ID3D12CommandQueue* m_godotCmdQueue = nullptr;  // Cached from Godot's RenderingDevice
#endif

    // Shared texture state (used by both Vulkan and D3D12 paths)
    bool m_sharedTextureEnabled = false;
    void* m_lastSharedHandle = nullptr;
    bool m_gpuTextureCreated = false;
    
    // Static CEF init flag
    static bool s_cefInitialized;
    
    // Static debug flag - must be set before CEF initializes (e.g., in _ready or earlier)
    static bool s_debugLogging;
public:
    static void set_debug_logging(bool enabled);
    static bool get_debug_logging();
};

} // namespace CefWebviewGodot
