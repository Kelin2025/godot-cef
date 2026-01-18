#include "cef_webview_node.h"
#include "cef_app.h"
#include "cef_client.h"

#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/engine.hpp>

#include "include/cef_browser.h"
#include "include/cef_request_context.h"

namespace CefWebviewGodot {

bool CefWebviewNode::s_cefInitialized = false;

void CefWebviewNode::_bind_methods() {
    using namespace godot;
    
    ClassDB::bind_method(D_METHOD("load_url", "url"), &CefWebviewNode::load_url);
    ClassDB::bind_method(D_METHOD("load_html", "html", "base_url"), &CefWebviewNode::load_html, DEFVAL(""));
    ClassDB::bind_method(D_METHOD("get_url"), &CefWebviewNode::get_url);
    ClassDB::bind_method(D_METHOD("execute_javascript", "script"), &CefWebviewNode::execute_javascript);
    ClassDB::bind_method(D_METHOD("is_gpu_accelerated"), &CefWebviewNode::is_gpu_accelerated);
    ClassDB::bind_method(D_METHOD("get_status"), &CefWebviewNode::get_status);
    
    ClassDB::bind_method(D_METHOD("set_url", "url"), &CefWebviewNode::set_url);
    ClassDB::bind_method(D_METHOD("get_initial_url"), &CefWebviewNode::get_initial_url);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "url"), "set_url", "get_initial_url");
    
    ClassDB::bind_method(D_METHOD("set_transparent", "transparent"), &CefWebviewNode::set_transparent);
    ClassDB::bind_method(D_METHOD("get_transparent"), &CefWebviewNode::get_transparent);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "transparent"), "set_transparent", "get_transparent");
    
    ClassDB::bind_method(D_METHOD("set_capture_keyboard", "capture"), &CefWebviewNode::set_capture_keyboard);
    ClassDB::bind_method(D_METHOD("get_capture_keyboard"), &CefWebviewNode::get_capture_keyboard);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "capture_keyboard"), "set_capture_keyboard", "get_capture_keyboard");
    
    // Signal for JS->GDScript communication
    // JS calls: window.cefQuery({ request: "your message", onSuccess: (response) => {}, onFailure: (err, msg) => {} })
    // GDScript receives: js_message(message: String) -> should return response string
    ADD_SIGNAL(MethodInfo("js_message", PropertyInfo(Variant::STRING, "message")));
}

CefWebviewNode::CefWebviewNode() = default;

CefWebviewNode::~CefWebviewNode() {
    if (m_clientPtr) {
        auto* clientRef = static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
        auto client = *clientRef;
        if (client && client->GetBrowser()) {
            client->GetBrowser()->GetHost()->CloseBrowser(true);
        }
        delete clientRef;
        m_clientPtr = nullptr;
    }
}

void CefWebviewNode::_ready() {
    godot::UtilityFunctions::print("[CEF] _ready() called");
    
    // Skip CEF initialization in editor - only run in game
    if (godot::Engine::get_singleton()->is_editor_hint()) {
        godot::UtilityFunctions::print("[CEF] Running in editor, skipping initialization");
        return;
    }
    
    // Initialize CEF once
    if (!s_cefInitialized) {
        if (!InitializeCef()) {
            godot::UtilityFunctions::print("[CEF] Failed to initialize CEF");
            return;
        }
        s_cefInitialized = true;
    }
    
    // Enable mouse input but don't take keyboard focus
    // Keys are forwarded via _input() instead of _gui_input() to avoid focus issues
    set_mouse_filter(MOUSE_FILTER_STOP);
    set_focus_mode(FOCUS_NONE);
    
    // Create browser
    createBrowser();
    
    m_initialized = true;
    m_previousSize = get_size();
}

void CefWebviewNode::createBrowser() {
    godot::Vector2 size = get_size();
    int width = size.x > 0 ? static_cast<int>(size.x) : 1280;
    int height = size.y > 0 ? static_cast<int>(size.y) : 720;
    
    godot::UtilityFunctions::print("[CEF] Creating browser ", width, "x", height);
    
    // Create render handler
    CefRefPtr<OffscreenRenderHandler> renderHandler = new OffscreenRenderHandler(width, height);
    
    // Create client
    CefRefPtr<GodotCefClient> client = new GodotCefClient(renderHandler);
    m_clientPtr = new CefRefPtr<GodotCefClient>(client);
    
    // Set up JS message callback - this bridges JS cefQuery to GDScript signal
    client->SetJsMessageCallback([this](const std::string& message) -> std::string {
        // Queue message for processing on main thread
        {
            std::lock_guard<std::mutex> lock(m_messageMutex);
            PendingJsMessage pending;
            pending.message = godot::String(message.c_str());
            pending.processed = false;
            m_pendingMessages.push_back(pending);
        }
        
        // Emit signal - this will be called from CEF thread, so we need deferred
        call_deferred("emit_signal", "js_message", godot::String(message.c_str()));
        
        // Return empty for now - async response not yet implemented
        // For sync response, user should use execute_javascript to send data back
        return "{}";
    });
    
    // Browser settings
    CefBrowserSettings browserSettings;
    browserSettings.windowless_frame_rate = 60;
    // Enable transparent background if requested
    if (m_transparent) {
        browserSettings.background_color = CefColorSetARGB(0, 0, 0, 0);
    }
    
    // Window info for offscreen rendering
    CefWindowInfo windowInfo;
    windowInfo.SetAsWindowless(0);
    
    // Create browser - resolve res:// paths to file:// URLs
    godot::String resolvedUrl = resolve_url(m_initialUrl);
    CefString url = resolvedUrl.utf8().get_data();
    CefBrowserHost::CreateBrowser(windowInfo, client, url, browserSettings, nullptr, nullptr);
    
    // Setup GPU texture
    setupGpuTexture();
    
    godot::UtilityFunctions::print("[CEF] Browser creation initiated");
}

void CefWebviewNode::setupGpuTexture() {
    auto* rs = godot::RenderingServer::get_singleton();
    auto* rd = rs->get_rendering_device();
    
    if (!rd) return;
    
    godot::Vector2 size = get_size();
    int width = size.x > 0 ? static_cast<int>(size.x) : 1280;
    int height = size.y > 0 ? static_cast<int>(size.y) : 720;
    
    // Free old texture
    if (m_rdTextureRid.is_valid() && rd->texture_is_valid(m_rdTextureRid)) {
        rd->free_rid(m_rdTextureRid);
    }
    
    // Create RD texture (BGRA format from CEF)
    godot::Ref<godot::RDTextureFormat> format;
    format.instantiate();
    format->set_format(godot::RenderingDevice::DATA_FORMAT_B8G8R8A8_UNORM);
    format->set_width(width);
    format->set_height(height);
    format->set_depth(1);
    format->set_array_layers(1);
    format->set_mipmaps(1);
    format->set_texture_type(godot::RenderingDevice::TEXTURE_TYPE_2D);
    format->set_samples(godot::RenderingDevice::TEXTURE_SAMPLES_1);
    format->set_usage_bits(
        godot::RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
        godot::RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT
    );
    
    godot::Ref<godot::RDTextureView> view;
    view.instantiate();
    
    m_rdTextureRid = rd->texture_create(format, view);
    
    if (rd->texture_is_valid(m_rdTextureRid)) {
        m_gpuTexture.instantiate();
        m_gpuTexture->set_texture_rd_rid(m_rdTextureRid);
        m_useGpuPath = true;
        godot::UtilityFunctions::print("[CEF] GPU texture created");
    }
}

void CefWebviewNode::_process(double delta) {
    if (!m_initialized || !m_clientPtr) return;
    
    // Update CEF message loop
    UpdateCef();
    
    // Handle resize
    godot::Vector2 size = get_size();
    if (size != m_previousSize && size.x > 0 && size.y > 0) {
        m_previousSize = size;
        
        auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
        if (client) {
            client->GetOffscreenRenderHandler()->Resize(static_cast<int>(size.x), static_cast<int>(size.y));
            if (client->GetBrowser()) {
                client->GetBrowser()->GetHost()->WasResized();
            }
        }
        setupGpuTexture();
    }
    
    // Update texture
    m_timeSinceUpdate += delta;
    if (m_timeSinceUpdate >= 0.016) {  // ~60fps
        m_timeSinceUpdate = 0.0;
        updateTexture();
        queue_redraw();
    }
}

void CefWebviewNode::updateTexture() {
    if (!m_useGpuPath || !m_clientPtr) return;
    
    auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
    if (!client) return;
    
    auto renderHandler = client->GetOffscreenRenderHandler();
    if (!renderHandler || !renderHandler->HasNewFrame()) return;
    
    renderHandler->ClearNewFrameFlag();
    
    const uint8_t* pixelData = renderHandler->GetPixelData();
    size_t dataSize = renderHandler->GetPixelDataSize();
    
    if (!pixelData || dataSize == 0) return;
    
    auto* rs = godot::RenderingServer::get_singleton();
    auto* rd = rs->get_rendering_device();
    
    if (rd && rd->texture_is_valid(m_rdTextureRid)) {
        godot::PackedByteArray bytes;
        bytes.resize(dataSize);
        memcpy(bytes.ptrw(), pixelData, dataSize);
        rd->texture_update(m_rdTextureRid, 0, bytes);
    }
}

void CefWebviewNode::_draw() {
    godot::Vector2 size = get_size();
    
    if (m_useGpuPath && m_gpuTexture.is_valid()) {
        draw_texture_rect(m_gpuTexture, godot::Rect2(godot::Vector2(), size), false);
    }
}

void CefWebviewNode::_gui_input(const godot::Ref<godot::InputEvent>& event) {
    if (!m_initialized || !m_clientPtr) return;
    
    auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
    if (!client || !client->GetBrowser()) return;
    
    // Only handle mouse in _gui_input (requires hovering over the control)
    if (auto motion = godot::Object::cast_to<godot::InputEventMouseMotion>(event.ptr())) {
        forwardMouseEvent(event);
    } else if (auto button = godot::Object::cast_to<godot::InputEventMouseButton>(event.ptr())) {
        forwardMouseEvent(event);
        // Release focus after mouse button release to prevent stealing keyboard input
        if (!button->is_pressed()) {
            release_focus();
        }
    }
}

void CefWebviewNode::_input(const godot::Ref<godot::InputEvent>& event) {
    if (!m_initialized || !m_clientPtr) return;
    
    auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
    if (!client || !client->GetBrowser()) return;
    
    // Forward key events to CEF regardless of focus
    // This allows JS to detect Shift, Escape, etc.
    // We don't mark as handled, so Godot also processes them for game controls
    if (auto key = godot::Object::cast_to<godot::InputEventKey>(event.ptr())) {
        forwardKeyEvent(event);
    }
}



void CefWebviewNode::forwardMouseEvent(const godot::Ref<godot::InputEvent>& event) {
    auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
    if (!client || !client->GetBrowser()) return;
    
    auto host = client->GetBrowser()->GetHost();
    godot::Vector2 localPos = get_local_mouse_position();
    
    CefMouseEvent mouseEvent;
    mouseEvent.x = static_cast<int>(localPos.x);
    mouseEvent.y = static_cast<int>(localPos.y);
    mouseEvent.modifiers = 0;
    
    if (auto motion = godot::Object::cast_to<godot::InputEventMouseMotion>(event.ptr())) {
        // For motion events, use button mask from the event
        int mask = motion->get_button_mask();
        if (mask & godot::MOUSE_BUTTON_MASK_LEFT) mouseEvent.modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
        if (mask & godot::MOUSE_BUTTON_MASK_RIGHT) mouseEvent.modifiers |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
        if (mask & godot::MOUSE_BUTTON_MASK_MIDDLE) mouseEvent.modifiers |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
        host->SendMouseMoveEvent(mouseEvent, false);
    } else if (auto button = godot::Object::cast_to<godot::InputEventMouseButton>(event.ptr())) {
        CefBrowserHost::MouseButtonType btnType = MBT_LEFT;
        bool isPressed = button->is_pressed();
        bool* statePtr = nullptr;
        
        switch (button->get_button_index()) {
            case godot::MOUSE_BUTTON_LEFT: 
                btnType = MBT_LEFT; 
                statePtr = &m_leftButtonDown;
                break;
            case godot::MOUSE_BUTTON_RIGHT: 
                btnType = MBT_RIGHT; 
                statePtr = &m_rightButtonDown;
                break;
            case godot::MOUSE_BUTTON_MIDDLE: 
                btnType = MBT_MIDDLE; 
                statePtr = &m_middleButtonDown;
                break;
            case godot::MOUSE_BUTTON_WHEEL_UP:
                host->SendMouseWheelEvent(mouseEvent, 0, 120);
                return;
            case godot::MOUSE_BUTTON_WHEEL_DOWN:
                host->SendMouseWheelEvent(mouseEvent, 0, -120);
                return;
            default: return;
        }
        
        // Filter duplicate events - only send if state actually changed
        if (statePtr && *statePtr == isPressed) {
            return; // State hasn't changed, ignore duplicate
        }
        if (statePtr) {
            *statePtr = isPressed;
        }
        
        bool mouseUp = !isPressed;
        host->SendMouseClickEvent(mouseEvent, btnType, mouseUp, 1);
    }
}

// Convert Godot keycode to Windows virtual key code
static int godotKeyToWindowsVK(godot::Key keycode) {
    using namespace godot;
    
    int code = static_cast<int>(keycode);
    
    // Handle special keys (Godot KEY_SPECIAL = 4194304)
    // These need explicit mapping to Windows VK codes
    if (code >= 4194304) {
        switch (keycode) {
            case KEY_ESCAPE: return 0x1B;      // VK_ESCAPE
            case KEY_TAB: return 0x09;         // VK_TAB
            case KEY_BACKTAB: return 0x09;     // VK_TAB (with shift)
            case KEY_BACKSPACE: return 0x08;   // VK_BACK
            case KEY_ENTER: return 0x0D;       // VK_RETURN
            case KEY_KP_ENTER: return 0x0D;    // VK_RETURN
            case KEY_INSERT: return 0x2D;      // VK_INSERT
            case KEY_DELETE: return 0x2E;      // VK_DELETE
            case KEY_PAUSE: return 0x13;       // VK_PAUSE
            case KEY_PRINT: return 0x2C;       // VK_SNAPSHOT
            case KEY_HOME: return 0x24;        // VK_HOME
            case KEY_END: return 0x23;         // VK_END
            case KEY_LEFT: return 0x25;        // VK_LEFT
            case KEY_UP: return 0x26;          // VK_UP
            case KEY_RIGHT: return 0x27;       // VK_RIGHT
            case KEY_DOWN: return 0x28;        // VK_DOWN
            case KEY_PAGEUP: return 0x21;      // VK_PRIOR
            case KEY_PAGEDOWN: return 0x22;    // VK_NEXT
            case KEY_SHIFT: return 0x10;       // VK_SHIFT
            case KEY_CTRL: return 0x11;        // VK_CONTROL
            case KEY_ALT: return 0x12;         // VK_MENU
            case KEY_META: return 0x5B;        // VK_LWIN
            case KEY_CAPSLOCK: return 0x14;    // VK_CAPITAL
            case KEY_NUMLOCK: return 0x90;     // VK_NUMLOCK
            case KEY_SCROLLLOCK: return 0x91;  // VK_SCROLL
            // Function keys
            case KEY_F1: return 0x70;
            case KEY_F2: return 0x71;
            case KEY_F3: return 0x72;
            case KEY_F4: return 0x73;
            case KEY_F5: return 0x74;
            case KEY_F6: return 0x75;
            case KEY_F7: return 0x76;
            case KEY_F8: return 0x77;
            case KEY_F9: return 0x78;
            case KEY_F10: return 0x79;
            case KEY_F11: return 0x7A;
            case KEY_F12: return 0x7B;
            // Numpad
            case KEY_KP_MULTIPLY: return 0x6A; // VK_MULTIPLY
            case KEY_KP_DIVIDE: return 0x6F;   // VK_DIVIDE
            case KEY_KP_SUBTRACT: return 0x6D; // VK_SUBTRACT
            case KEY_KP_ADD: return 0x6B;      // VK_ADD
            case KEY_KP_PERIOD: return 0x6E;   // VK_DECIMAL
            case KEY_KP_0: return 0x60;        // VK_NUMPAD0
            case KEY_KP_1: return 0x61;
            case KEY_KP_2: return 0x62;
            case KEY_KP_3: return 0x63;
            case KEY_KP_4: return 0x64;
            case KEY_KP_5: return 0x65;
            case KEY_KP_6: return 0x66;
            case KEY_KP_7: return 0x67;
            case KEY_KP_8: return 0x68;
            case KEY_KP_9: return 0x69;
            case KEY_MENU: return 0x5D;        // VK_APPS
            default:
                return 0; // Unknown special key
        }
    }
    
    // ASCII letters (A-Z) - Godot uses uppercase ASCII (65-90)
    if (code >= 'A' && code <= 'Z') {
        return code; // Same as Windows VK
    }
    
    // ASCII digits (0-9) - Godot uses ASCII (48-57)
    if (code >= '0' && code <= '9') {
        return code; // Same as Windows VK
    }
    
    // Punctuation and other ASCII keys need mapping to VK_OEM codes
    switch (code) {
        case ' ': return 0x20;       // VK_SPACE
        case '-': return 0xBD;       // VK_OEM_MINUS
        case '=': return 0xBB;       // VK_OEM_PLUS (equals key)
        case '[': return 0xDB;       // VK_OEM_4
        case ']': return 0xDD;       // VK_OEM_6
        case '\\': return 0xDC;      // VK_OEM_5
        case ';': return 0xBA;       // VK_OEM_1
        case '\'': return 0xDE;      // VK_OEM_7
        case ',': return 0xBC;       // VK_OEM_COMMA
        case '.': return 0xBE;       // VK_OEM_PERIOD
        case '/': return 0xBF;       // VK_OEM_2
        case '`': return 0xC0;       // VK_OEM_3 (backtick/tilde)
        default:
            return code; // Fallback to raw code
    }
}

void CefWebviewNode::forwardKeyEvent(const godot::Ref<godot::InputEvent>& event) {
    auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
    if (!client || !client->GetBrowser()) return;
    
    auto key = godot::Object::cast_to<godot::InputEventKey>(event.ptr());
    if (!key) return;
    
    auto host = client->GetBrowser()->GetHost();
    
    int vkCode = godotKeyToWindowsVK(key->get_keycode());
    
    CefKeyEvent keyEvent;
    keyEvent.windows_key_code = vkCode;
    keyEvent.native_key_code = godotKeyToWindowsVK(key->get_physical_keycode());
    keyEvent.modifiers = 0;
    keyEvent.is_system_key = false;
    keyEvent.character = 0;
    keyEvent.unmodified_character = 0;
    
    // Set modifier flags based on current state
    if (key->is_shift_pressed()) keyEvent.modifiers |= EVENTFLAG_SHIFT_DOWN;
    if (key->is_ctrl_pressed()) keyEvent.modifiers |= EVENTFLAG_CONTROL_DOWN;
    if (key->is_alt_pressed()) keyEvent.modifiers |= EVENTFLAG_ALT_DOWN;
    if (key->is_meta_pressed()) keyEvent.modifiers |= EVENTFLAG_COMMAND_DOWN;
    
    // Mark as system key if Alt is pressed (for Alt+key combinations)
    if (key->is_alt_pressed()) {
        keyEvent.is_system_key = true;
    }
    
    if (key->is_pressed()) {
        keyEvent.type = KEYEVENT_RAWKEYDOWN;
        host->SendKeyEvent(keyEvent);
        
        // Send CHAR event for printable characters
        char32_t unicode = key->get_unicode();
        if (unicode > 0) {
            keyEvent.type = KEYEVENT_CHAR;
            keyEvent.character = static_cast<char16_t>(unicode);
            keyEvent.unmodified_character = static_cast<char16_t>(unicode);
            keyEvent.windows_key_code = static_cast<int>(unicode);
            host->SendKeyEvent(keyEvent);
        }
    } else {
        keyEvent.type = KEYEVENT_KEYUP;
        host->SendKeyEvent(keyEvent);
    }
}

void CefWebviewNode::load_url(const godot::String& url) {
    m_initialUrl = url;
    
    if (m_clientPtr) {
        auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
        if (client && client->GetBrowser()) {
            godot::String finalUrl = resolve_url(url);
            CefString cefUrl = finalUrl.utf8().get_data();
            client->GetBrowser()->GetMainFrame()->LoadURL(cefUrl);
        }
    }
}

godot::String CefWebviewNode::resolve_url(const godot::String& url) const {
    // Convert res:// and user:// paths to file:// URLs
    if (url.begins_with("res://") || url.begins_with("user://")) {
        // First check if file exists via Godot's VFS
        if (!godot::FileAccess::file_exists(url)) {
            godot::UtilityFunctions::push_warning("[CEF] File not found: ", url);
            return url;
        }
        
        // Get absolute path
        godot::String absolutePath = godot::ProjectSettings::get_singleton()->globalize_path(url);
        
        // Check if this is a real absolute path (contains : on Windows or starts with / on Unix)
        // If not, it means the file is inside a .pck and globalize_path returned a relative path
        bool isRealAbsolutePath = absolutePath.contains(":") || absolutePath.begins_with("/");
        
        if (isRealAbsolutePath) {
            // File exists on disk - use file:// URL
            absolutePath = absolutePath.replace("\\", "/");
            if (!absolutePath.begins_with("/")) {
                return "file:///" + absolutePath;
            }
            return "file://" + absolutePath;
        }
        
        // File is in .pck - read content and use data: URL
        godot::UtilityFunctions::print("[CEF] File is in .pck, using data: URL for: ", url);
        auto file = godot::FileAccess::open(url, godot::FileAccess::READ);
        if (file.is_valid()) {
            godot::String content = file->get_as_text();
            // Determine MIME type from extension
            godot::String mimeType = "text/html";
            if (url.ends_with(".css")) {
                mimeType = "text/css";
            } else if (url.ends_with(".js")) {
                mimeType = "application/javascript";
            } else if (url.ends_with(".json")) {
                mimeType = "application/json";
            }
            // URL-encode the content for data URL
            godot::String encoded = content.uri_encode();
            return "data:" + mimeType + ";charset=utf-8," + encoded;
        }
        
        godot::UtilityFunctions::push_warning("[CEF] Could not resolve path: ", url);
        return url;
    }
    return url;
}

void CefWebviewNode::load_html(const godot::String& html, const godot::String& base_url) {
    if (m_clientPtr) {
        auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
        if (client && client->GetBrowser()) {
            CefString cefHtml = html.utf8().get_data();
            CefString cefBaseUrl = base_url.is_empty() ? "about:blank" : base_url.utf8().get_data();
            client->GetBrowser()->GetMainFrame()->LoadURL(
                CefString("data:text/html;charset=utf-8," + std::string(cefHtml.ToString()))
            );
        }
    }
}

godot::String CefWebviewNode::get_url() const {
    return m_initialUrl;
}

void CefWebviewNode::execute_javascript(const godot::String& script) {
    if (m_clientPtr) {
        auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
        if (client && client->GetBrowser()) {
            CefString cefScript = script.utf8().get_data();
            client->GetBrowser()->GetMainFrame()->ExecuteJavaScript(cefScript, "", 0);
        }
    }
}

void CefWebviewNode::set_url(const godot::String& url) {
    m_initialUrl = url;
    if (m_initialized) {
        load_url(url);
    }
}

godot::String CefWebviewNode::get_initial_url() const {
    return m_initialUrl;
}

void CefWebviewNode::set_transparent(bool transparent) {
    m_transparent = transparent;
}

bool CefWebviewNode::get_transparent() const {
    return m_transparent;
}

void CefWebviewNode::set_capture_keyboard(bool capture) {
    m_captureKeyboard = capture;
}

bool CefWebviewNode::get_capture_keyboard() const {
    return m_captureKeyboard;
}

godot::String CefWebviewNode::get_status() const {
    if (!m_initialized) {
        return "Not initialized";
    }
    if (m_useGpuPath) {
        return "CEF GPU rendering";
    }
    return "CEF CPU rendering";
}

} // namespace CefWebviewGodot
