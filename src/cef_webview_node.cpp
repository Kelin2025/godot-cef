#include "cef_webview_node.h"
#include "cef_app.h"
#include "cef_client.h"
#include "cef_debug.h"

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
#include <godot_cpp/classes/time.hpp>

#include "include/cef_browser.h"
#include "include/cef_request_context.h"

#ifdef CEF_USE_VULKAN_INTEROP
#include "vulkan_interop.h"
#endif

#ifdef CEF_USE_D3D12_INTEROP
#include "d3d12_interop.h"
#endif

namespace CefWebviewGodot {

bool CefWebviewNode::s_cefInitialized = false;
bool CefWebviewNode::s_debugLogging = false;

// Global accessor for other files
bool GetDebugLogging() {
    return CefWebviewNode::get_debug_logging();
}

void CefWebviewNode::set_debug_logging(bool enabled) {
    s_debugLogging = enabled;
    SetDebugLogging(enabled);  // Also set the shared flag
}

bool CefWebviewNode::get_debug_logging() {
    return s_debugLogging;
}

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
    
    ClassDB::bind_method(D_METHOD("set_handle_mouse", "handle"), &CefWebviewNode::set_handle_mouse);
    ClassDB::bind_method(D_METHOD("get_handle_mouse"), &CefWebviewNode::get_handle_mouse);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "handle_mouse"), "set_handle_mouse", "get_handle_mouse");
    
    ClassDB::bind_method(D_METHOD("set_handle_keys", "handle"), &CefWebviewNode::set_handle_keys);
    ClassDB::bind_method(D_METHOD("get_handle_keys"), &CefWebviewNode::get_handle_keys);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "handle_keys"), "set_handle_keys", "get_handle_keys");
    
    ClassDB::bind_method(D_METHOD("set_frame_rate", "fps"), &CefWebviewNode::set_frame_rate);
    ClassDB::bind_method(D_METHOD("get_frame_rate"), &CefWebviewNode::get_frame_rate);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "frame_rate", godot::PROPERTY_HINT_RANGE, "1,120,1"), "set_frame_rate", "get_frame_rate");
    
    ClassDB::bind_method(D_METHOD("set_paused", "paused"), &CefWebviewNode::set_paused);
    ClassDB::bind_method(D_METHOD("get_paused"), &CefWebviewNode::get_paused);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "paused"), "set_paused", "get_paused");
    
    // Static debug logging setting - must be set before first CefWebviewNode is created
    ClassDB::bind_static_method("CefWebviewNode", D_METHOD("set_debug_logging", "enabled"), &CefWebviewNode::set_debug_logging);
    ClassDB::bind_static_method("CefWebviewNode", D_METHOD("get_debug_logging"), &CefWebviewNode::get_debug_logging);
    
    // Signal for JS->GDScript communication
    // JS calls: window.cefQuery({ request: "your message", onSuccess: (response) => {}, onFailure: (err, msg) => {} })
    // GDScript receives: js_message(message: String) -> should return response string
    ADD_SIGNAL(MethodInfo("js_message", PropertyInfo(Variant::STRING, "message")));
    
    // Signal emitted when page load finishes
    ADD_SIGNAL(MethodInfo("load_finished"));
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
    CEF_DEBUG_PRINT("[CEF] _ready() called");
    
    // Skip CEF initialization in editor - only run in game
    if (godot::Engine::get_singleton()->is_editor_hint()) {
        CEF_DEBUG_PRINT("[CEF] Running in editor, skipping initialization");
        return;
    }
    
    // Initialize CEF once
    if (!s_cefInitialized) {
        if (!InitializeCef()) {
            CEF_DEBUG_PRINT("[CEF] Failed to initialize CEF");
            return;
        }
        s_cefInitialized = true;
    }
    
    // Enable mouse input but don't take keyboard focus
    // Keys are forwarded via _input() instead of _gui_input() to avoid focus issues
    set_mouse_filter(MOUSE_FILTER_STOP);
    set_focus_mode(FOCUS_NONE);
    
    // Hide until page loads to prevent showing unrendered content
    set_visible(false);
    
    // Create browser
    createBrowser();
    
    m_initialized = true;
    m_previousSize = get_size();
}

void CefWebviewNode::createBrowser() {
    godot::Vector2 size = get_size();
    int width = size.x > 0 ? static_cast<int>(size.x) : 1280;
    int height = size.y > 0 ? static_cast<int>(size.y) : 720;
    
    CEF_DEBUG_PRINT("[CEF] Creating browser ", width, "x", height);
    
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
    
    // Set up load finished callback - emits signal when page fully loads
    client->SetLoadFinishedCallback([this]() {
        // Show the control now that the page is rendered
        call_deferred("set_visible", true);
        call_deferred("emit_signal", "load_finished");
    });
    
    // Browser settings
    CefBrowserSettings browserSettings;
    browserSettings.windowless_frame_rate = m_frameRate;
    // Enable transparent background if requested
    if (m_transparent) {
        browserSettings.background_color = CefColorSetARGB(0, 0, 0, 0);
    }
    
    // Window info for offscreen rendering
    CefWindowInfo windowInfo;
    windowInfo.SetAsWindowless(0);
    
    // Check if GPU shared textures can be enabled
    bool canUseSharedTexture = false;
    
    auto* rs = godot::RenderingServer::get_singleton();
    auto* rd = rs ? rs->get_rendering_device() : nullptr;
    
    if (rd) {
#ifdef CEF_USE_D3D12_INTEROP
        // Check for D3D12 backend
        uint64_t d3d12Device = rd->get_driver_resource(
            godot::RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE, godot::RID(), 0
        );
        uint64_t vkInstance = rd->get_driver_resource(
            godot::RenderingDevice::DRIVER_RESOURCE_VULKAN_INSTANCE, godot::RID(), 0
        );
        
        if (d3d12Device && vkInstance == 0) {
            // D3D12 backend detected - use copy-to-Godot-texture approach
            CEF_DEBUG_PRINT("[CEF] D3D12 backend detected, enabling GPU shared textures");
            canUseSharedTexture = true;
        }
#endif
    }
    
    if (canUseSharedTexture) {
        windowInfo.shared_texture_enabled = true;
        windowInfo.external_begin_frame_enabled = true;
        renderHandler->SetSharedTextureEnabled(true);
        m_sharedTextureEnabled = true;
        CEF_DEBUG_PRINT("[CEF] Shared texture ENABLED (copy-to-Godot approach)");
        CEF_DEBUG_PRINT("[CEF] External begin frame ENABLED");
    } else {
        m_sharedTextureEnabled = false;
        CEF_DEBUG_PRINT("[CEF] Using CPU copy path");
    }
    
    // Create browser - resolve res:// paths to file:// URLs
    godot::String resolvedUrl = resolve_url(m_initialUrl);
    CefString url = resolvedUrl.utf8().get_data();
    CefBrowserHost::CreateBrowser(windowInfo, client, url, browserSettings, nullptr, nullptr);
    
    // Setup GPU texture (will be replaced by shared texture if available)
    setupGpuTexture();
    
    CEF_DEBUG_PRINT("[CEF] Browser creation initiated");
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
    
#ifdef CEF_USE_D3D12_INTEROP
    // Try D3D12 interop (when Godot is using D3D12 backend)
    // NOTE: texture_create_from_extension may fail - see docs/GPU_SHARED_TEXTURE_IMPLEMENTATION.md
    if (!m_d3d12Interop && m_sharedTextureEnabled) {
        uint64_t deviceHandle = rd->get_driver_resource(
            godot::RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE, godot::RID(), 0
        );
        uint64_t vkInstance = rd->get_driver_resource(
            godot::RenderingDevice::DRIVER_RESOURCE_VULKAN_INSTANCE, godot::RID(), 0
        );
        
        // If Vulkan instance is 0, we're on D3D12
        if (deviceHandle && vkInstance == 0) {
            CEF_DEBUG_PRINT("[CEF] Detected D3D12 rendering backend");
            m_d3d12Interop = std::make_unique<D3D12Interop>();
            
            if (m_d3d12Interop->Initialize(reinterpret_cast<void*>(deviceHandle))) {
                CEF_DEBUG_PRINT("[CEF] D3D12 interop initialized");
                
                // Pass D3D12Interop to render handler for immediate texture copy
                auto* clientRef = static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
                if (clientRef && *clientRef) {
                    auto renderHandler = (*clientRef)->GetOffscreenRenderHandler();
                    if (renderHandler) {
                        renderHandler->SetD3D12Interop(m_d3d12Interop.get());
                        CEF_DEBUG_PRINT("[CEF] D3D12Interop passed to render handler");
                    }
                }
                
                m_useGpuPath = true;
                return;
            } else {
                CEF_DEBUG_PRINT("[CEF] D3D12 interop failed to initialize");
                m_d3d12Interop.reset();
            }
        }
    }
    
    // If D3D12 interop is active, don't create CPU texture
    if (m_sharedTextureEnabled && m_d3d12Interop) {
        CEF_DEBUG_PRINT("[CEF] Using D3D12 shared GPU texture path");
        m_useGpuPath = true;
        return;
    }
#endif

#ifdef CEF_USE_VULKAN_INTEROP
    // Try Vulkan interop (requires VK_KHR_external_memory_win32 to be enabled in Godot)
    if (!m_vulkanInterop && m_sharedTextureEnabled) {
        m_vulkanInterop = std::make_unique<VulkanInterop>();
        
        // Get Vulkan handles from Godot's RenderingDevice
        VkInstance vkInstance = reinterpret_cast<VkInstance>(
            rd->get_driver_resource(godot::RenderingDevice::DRIVER_RESOURCE_VULKAN_INSTANCE, godot::RID(), 0)
        );
        VkPhysicalDevice vkPhysicalDevice = reinterpret_cast<VkPhysicalDevice>(
            rd->get_driver_resource(godot::RenderingDevice::DRIVER_RESOURCE_VULKAN_PHYSICAL_DEVICE, godot::RID(), 0)
        );
        VkDevice vkDevice = reinterpret_cast<VkDevice>(
            rd->get_driver_resource(godot::RenderingDevice::DRIVER_RESOURCE_VULKAN_DEVICE, godot::RID(), 0)
        );
        
        if (vkInstance && vkPhysicalDevice && vkDevice) {
            if (m_vulkanInterop->Initialize(vkInstance, vkPhysicalDevice, vkDevice)) {
                CEF_DEBUG_PRINT("[CEF] Vulkan interop initialized");
            } else {
                CEF_DEBUG_PRINT("[CEF] Vulkan interop failed to initialize, falling back to CPU");
                m_vulkanInterop.reset();
                m_sharedTextureEnabled = false;
            }
        } else {
            CEF_DEBUG_PRINT("[CEF] Failed to get Vulkan handles from Godot (not running Vulkan?)");
            m_vulkanInterop.reset();
            m_sharedTextureEnabled = false;
        }
    }
    
    // If using Vulkan shared texture path
    if (m_sharedTextureEnabled && m_vulkanInterop && m_vulkanInterop->IsExternalMemorySupported()) {
        CEF_DEBUG_PRINT("[CEF] Using Vulkan shared GPU texture path");
        m_useGpuPath = true;
        return;
    }
#endif
    
    // CPU path: Create RD texture (BGRA format from CEF)
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
        CEF_DEBUG_PRINT("[CEF] GPU texture created (CPU copy path)");
    }
}

void CefWebviewNode::_process(double delta) {
    if (!m_initialized || !m_clientPtr) return;
    if (m_paused) return;  // Skip all processing when paused
    
    // Update CEF message loop
    UpdateCef();
    
    // Send external begin frame to trigger rendering when using shared textures
#if defined(CEF_USE_VULKAN_INTEROP) || defined(CEF_USE_D3D12_INTEROP)
    if (m_sharedTextureEnabled) {
        auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
        if (client && client->GetBrowser()) {
            client->GetBrowser()->GetHost()->SendExternalBeginFrame();
        }
    }
#endif
    
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
    
    // Update texture only when CEF has a new frame
    auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
    if (client) {
        auto renderHandler = client->GetOffscreenRenderHandler();
        if (renderHandler) {
            // Check both CPU path (HasNewFrame) and GPU shared texture path (HasNewSharedFrame)
            bool hasNewFrame = renderHandler->HasNewFrame() || renderHandler->HasNewSharedFrame();
            if (hasNewFrame) {
                updateTexture();
                queue_redraw();
            }
        }
    }
}

void CefWebviewNode::updateTexture() {
    if (!m_initialized || !m_useGpuPath || !m_clientPtr) return;
    
    auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
    if (!client) return;
    
    auto renderHandler = client->GetOffscreenRenderHandler();
    if (!renderHandler) return;
    
#ifdef CEF_USE_D3D12_INTEROP
    // D3D12 shared texture path - COPY TO GODOT-OWNED TEXTURE
    // We create a Godot texture, get its D3D12 resource, and copy TO it
    if (m_sharedTextureEnabled && m_d3d12Interop) {
        bool hasNewFrame = renderHandler->HasNewSharedFrame();
        
        if (!hasNewFrame) {
            return;
        }
        
        renderHandler->ClearNewSharedFrameFlag();
        
        // The texture copy from CEF was done in OnAcceleratedPaint
        // Now we need to copy from our shared texture TO a Godot-owned texture
        if (m_d3d12Interop->HasValidTexture()) {
            auto* rs = godot::RenderingServer::get_singleton();
            auto* rd = rs ? rs->get_rendering_device() : nullptr;
            if (!rd) return;
            
            godot::Vector2 size = get_size();
            int width = size.x > 0 ? static_cast<int>(size.x) : 1280;
            int height = size.y > 0 ? static_cast<int>(size.y) : 720;
            
            // Create Godot-owned texture if needed
            if (!m_gpuTextureCreated) {
                // Create a Godot RD texture that Godot owns and allocates
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
                    godot::RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT
                );
                
                godot::Ref<godot::RDTextureView> view;
                view.instantiate();
                m_rdTextureRid = rd->texture_create(format, view);
                
                if (rd->texture_is_valid(m_rdTextureRid)) {
                    if (!m_gpuTexture.is_valid()) {
                        m_gpuTexture.instantiate();
                    }
                    m_gpuTexture->set_texture_rd_rid(m_rdTextureRid);
                    m_gpuTextureCreated = true;
                    
                    // Get Godot's command queue
                    // Note: We need a valid RID for the command queue - use the render queue
                    m_godotCmdQueue = reinterpret_cast<ID3D12CommandQueue*>(
                        rd->get_driver_resource(godot::RenderingDevice::DRIVER_RESOURCE_COMMAND_QUEUE, godot::RID(), 0)
                    );
                    
                    CEF_DEBUG_PRINT("[CEF D3D12] Created Godot-owned texture: ", width, "x", height);
                    CEF_DEBUG_PRINT("[CEF D3D12] Command queue: ", m_godotCmdQueue ? "valid" : "null");
                } else {
                    CEF_DEBUG_PRINT("[CEF D3D12] Failed to create Godot texture");
                    return;
                }
            }
            
            // Get Godot's D3D12 resource
            ID3D12Resource* godotResource = reinterpret_cast<ID3D12Resource*>(
                rd->get_driver_resource(godot::RenderingDevice::DRIVER_RESOURCE_TEXTURE, m_rdTextureRid, 0)
            );
            
            if (godotResource && m_godotCmdQueue) {
                // Copy from our shared texture TO Godot's texture
                static int copyCount = 0;
                if (m_d3d12Interop->CopyToGodotTexture(godotResource, m_godotCmdQueue)) {
                    if (++copyCount <= 5 || copyCount % 60 == 0) {
                        CEF_DEBUG_PRINT("[CEF D3D12] Copied to Godot texture, count=", copyCount);
                    }
                }
            }
        }
        
        queue_redraw();
        return;
    }
#endif

#ifdef CEF_USE_VULKAN_INTEROP
    // Check if using shared texture path (GPU accelerated)
    if (m_sharedTextureEnabled && m_vulkanInterop && renderHandler->HasNewSharedFrame()) {
        renderHandler->ClearNewSharedFrameFlag();
        void* sharedHandle = renderHandler->GetSharedHandle();
        
        if (sharedHandle && sharedHandle != m_lastSharedHandle) {
            // New shared handle - need to import it
            m_lastSharedHandle = sharedHandle;
            
            auto* rs = godot::RenderingServer::get_singleton();
            auto* rd = rs ? rs->get_rendering_device() : nullptr;
            if (!rd) return;
            
            godot::Vector2 size = get_size();
            int width = size.x > 0 ? static_cast<int>(size.x) : 1280;
            int height = size.y > 0 ? static_cast<int>(size.y) : 720;
            
            // Import the D3D11 shared texture into Vulkan
            ImportedVulkanTexture imported = m_vulkanInterop->ImportSharedTexture(
                static_cast<HANDLE>(sharedHandle),
                width,
                height,
                VK_FORMAT_B8G8R8A8_UNORM
            );
            
            if (imported.valid) {
                m_vulkanImportFailCount = 0; // Reset fail counter on success
                
                // Free old RD texture if it exists
                if (m_rdTextureRid.is_valid() && rd->texture_is_valid(m_rdTextureRid)) {
                    rd->free_rid(m_rdTextureRid);
                }
                
                // Create a Godot RD texture from our imported VkImage
                // texture_create_from_extension takes the raw VkImage handle
                uint64_t vkImageHandle = reinterpret_cast<uint64_t>(imported.image);
                
                m_rdTextureRid = rd->texture_create_from_extension(
                    godot::RenderingDevice::TEXTURE_TYPE_2D,
                    godot::RenderingDevice::DATA_FORMAT_B8G8R8A8_UNORM,
                    godot::RenderingDevice::TEXTURE_SAMPLES_1,
                    godot::RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT,
                    vkImageHandle,
                    static_cast<uint64_t>(width),
                    static_cast<uint64_t>(height),
                    1,  // depth
                    1,  // layers
                    1   // mipmaps
                );
                
                if (rd->texture_is_valid(m_rdTextureRid)) {
                    // Update Texture2DRD to use our new RID
                    if (!m_gpuTexture.is_valid()) {
                        m_gpuTexture.instantiate();
                    }
                    m_gpuTexture->set_texture_rd_rid(m_rdTextureRid);
                    CEF_DEBUG_PRINT("[CEF] Shared GPU texture registered with Godot ", width, "x", height);
                } else {
                    CEF_DEBUG_PRINT("[CEF] Failed to create Godot texture from VkImage");
                }
            } else {
                // Import failed - D3D11 NTHANDLE cannot be imported into Vulkan
                m_vulkanImportFailCount++;
                if (m_vulkanImportFailCount >= 3) {
                    CEF_DEBUG_PRINT("[CEF Vulkan] Import failed 3 times, disabling Vulkan interop");
                    CEF_DEBUG_PRINT("[CEF Vulkan] CEF's D3D11 NTHANDLE is not compatible with Vulkan");
                    CEF_DEBUG_PRINT("[CEF Vulkan] Switching to CPU copy path");
                    
                    // Disable Vulkan interop and switch to CPU path
                    m_vulkanInterop.reset();
                    m_sharedTextureEnabled = false;
                    renderHandler->SetSharedTextureEnabled(false);
                    
                    // Create CPU texture
                    setupGpuTexture();
                    return;
                }
            }
        }
        
        // Trigger redraw for shared texture path
        queue_redraw();
        return; // Using shared texture path, don't use CPU path
    }
#endif
    
    // CPU path - copy pixel data to GPU texture
    if (!renderHandler->HasNewFrame()) return;
    
    renderHandler->ClearNewFrameFlag();
    
    const uint8_t* pixelData = renderHandler->GetPixelData();
    size_t dataSize = renderHandler->GetPixelDataSize();
    
    if (!pixelData || dataSize == 0) return;
    
    auto* rs = godot::RenderingServer::get_singleton();
    if (!rs) return;
    
    auto* rd = rs->get_rendering_device();
    if (!rd) return;
    
    if (!m_rdTextureRid.is_valid() || !rd->texture_is_valid(m_rdTextureRid)) return;
    
    // Reuse the byte array to avoid allocation overhead
    if (m_textureBytes.size() != static_cast<int64_t>(dataSize)) {
        m_textureBytes.resize(dataSize);
    }
    memcpy(m_textureBytes.ptrw(), pixelData, dataSize);
    rd->texture_update(m_rdTextureRid, 0, m_textureBytes);
}

void CefWebviewNode::_draw() {
    godot::Vector2 size = get_size();
    
    if (m_useGpuPath && m_gpuTexture.is_valid()) {
        draw_texture_rect(m_gpuTexture, godot::Rect2(godot::Vector2(), size), false);
    }
}

void CefWebviewNode::_notification(int what) {
    // Reset mouse button states when mouse exits the control
    // Only use NOTIFICATION_MOUSE_EXIT, not FOCUS_EXIT (focus changes frequently due to release_focus())
    if (what == NOTIFICATION_MOUSE_EXIT) {
        // Send mouse up events for any buttons that are still pressed
        if (m_initialized && m_clientPtr) {
            auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
            if (client && client->GetBrowser()) {
                auto host = client->GetBrowser()->GetHost();
                if (host) {
                    CefMouseEvent mouseEvent;
                    mouseEvent.x = 0;
                    mouseEvent.y = 0;
                    mouseEvent.modifiers = 0;
                    
                    if (m_leftButtonDown) {
                        host->SendMouseClickEvent(mouseEvent, MBT_LEFT, true, 1);
                        m_leftButtonDown = false;
                    }
                    if (m_rightButtonDown) {
                        host->SendMouseClickEvent(mouseEvent, MBT_RIGHT, true, 1);
                        m_rightButtonDown = false;
                    }
                    if (m_middleButtonDown) {
                        host->SendMouseClickEvent(mouseEvent, MBT_MIDDLE, true, 1);
                        m_middleButtonDown = false;
                    }
                }
            }
        }
    }
}

void CefWebviewNode::_gui_input(const godot::Ref<godot::InputEvent>& event) {
    if (!m_initialized || !m_clientPtr) return;
    if (!m_handleMouse) return;  // Skip if mouse handling is disabled
    
    auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
    if (!client || !client->GetBrowser()) return;
    
    // Handle mouse motion
    if (auto motion = godot::Object::cast_to<godot::InputEventMouseMotion>(event.ptr())) {
        forwardMouseEvent(event);
    } else if (auto button = godot::Object::cast_to<godot::InputEventMouseButton>(event.ptr())) {
        bool isPressed = button->is_pressed();
        int btnIndex = button->get_button_index();
        
        // Time-based debouncing - reject clicks that come too fast (CEF feedback loop)
        uint64_t now = godot::Time::get_singleton()->get_ticks_msec();
        if (now - m_lastClickTime < CLICK_DEBOUNCE_MS) {
            // Too fast - likely a feedback loop
            return;
        }
        m_lastClickTime = now;
        
        // Get current state for this button
        bool* statePtr = nullptr;
        switch (btnIndex) {
            case godot::MOUSE_BUTTON_LEFT: statePtr = &m_leftButtonDown; break;
            case godot::MOUSE_BUTTON_RIGHT: statePtr = &m_rightButtonDown; break;
            case godot::MOUSE_BUTTON_MIDDLE: statePtr = &m_middleButtonDown; break;
            default: break;
        }
        
        // Only forward if state actually changes
        if (statePtr) {
            if (*statePtr == isPressed) {
                // State hasn't changed, ignore duplicate
                return;
            }
            *statePtr = isPressed;
        }
        
        forwardMouseEvent(event);
    }
}

void CefWebviewNode::_input(const godot::Ref<godot::InputEvent>& event) {
    // Not used
}

void CefWebviewNode::_unhandled_input(const godot::Ref<godot::InputEvent>& event) {
    if (!m_initialized || !m_clientPtr) return;
    if (!m_handleKeys) return;  // Skip if key handling is disabled
    
    auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
    if (!client || !client->GetBrowser()) return;
    
    // Forward key events to CEF via _unhandled_input
    // This allows game to process keys first, CEF gets them after
    if (auto key = godot::Object::cast_to<godot::InputEventKey>(event.ptr())) {
        forwardKeyEvent(event);
    }
}



void CefWebviewNode::forwardMouseEvent(const godot::Ref<godot::InputEvent>& event) {
    if (!m_initialized || !m_clientPtr) return;
    
    auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
    if (!client || !client->GetBrowser()) return;
    
    auto host = client->GetBrowser()->GetHost();
    if (!host) return;
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
        
        switch (button->get_button_index()) {
            case godot::MOUSE_BUTTON_LEFT: 
                btnType = MBT_LEFT; 
                break;
            case godot::MOUSE_BUTTON_RIGHT: 
                btnType = MBT_RIGHT; 
                break;
            case godot::MOUSE_BUTTON_MIDDLE: 
                btnType = MBT_MIDDLE; 
                break;
            case godot::MOUSE_BUTTON_WHEEL_UP:
                host->SendMouseWheelEvent(mouseEvent, 0, 120);
                return;
            case godot::MOUSE_BUTTON_WHEEL_DOWN:
                host->SendMouseWheelEvent(mouseEvent, 0, -120);
                return;
            default: return;
        }
        
        // State tracking is done in _gui_input before calling this function
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
    if (!m_initialized || !m_clientPtr) return;
    
    auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
    if (!client || !client->GetBrowser()) return;
    
    auto key = godot::Object::cast_to<godot::InputEventKey>(event.ptr());
    if (!key) return;
    
    auto host = client->GetBrowser()->GetHost();
    if (!host) return;
    
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
        CEF_DEBUG_PRINT("[CEF] File is in .pck, using data: URL for: ", url);
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

void CefWebviewNode::set_handle_mouse(bool handle) {
    m_handleMouse = handle;
}

bool CefWebviewNode::get_handle_mouse() const {
    return m_handleMouse;
}

void CefWebviewNode::set_handle_keys(bool handle) {
    m_handleKeys = handle;
}

bool CefWebviewNode::get_handle_keys() const {
    return m_handleKeys;
}

void CefWebviewNode::set_frame_rate(int fps) {
    m_frameRate = fps;
    // Update browser frame rate if already created
    if (m_clientPtr) {
        auto client = *static_cast<CefRefPtr<GodotCefClient>*>(m_clientPtr);
        if (client && client->GetBrowser()) {
            client->GetBrowser()->GetHost()->SetWindowlessFrameRate(fps);
        }
    }
}

int CefWebviewNode::get_frame_rate() const {
    return m_frameRate;
}

void CefWebviewNode::set_paused(bool paused) {
    m_paused = paused;
}

bool CefWebviewNode::get_paused() const {
    return m_paused;
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
