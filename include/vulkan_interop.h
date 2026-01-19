#pragma once

#ifdef CEF_USE_VULKAN_INTEROP

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <windows.h>
#include <cstdint>

namespace CefWebviewGodot {

// Imported texture from D3D11 shared handle
struct ImportedVulkanTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    bool valid = false;
};

// Vulkan interop manager for importing D3D11 shared textures
class VulkanInterop {
public:
    VulkanInterop();
    ~VulkanInterop();

    // Initialize with Vulkan handles from Godot's RenderingDevice
    // These are obtained via rd->get_driver_resource()
    bool Initialize(
        VkInstance instance,
        VkPhysicalDevice physicalDevice,
        VkDevice device
    );
    
    // Check if external memory extension is supported
    bool IsExternalMemorySupported() const { return m_externalMemorySupported; }
    
    // Import a D3D11 shared texture handle into Vulkan
    // The shared_handle comes from CEF's OnAcceleratedPaint callback
    // Returns an ImportedVulkanTexture that can be used for rendering
    ImportedVulkanTexture ImportSharedTexture(
        HANDLE sharedHandle,
        uint32_t width,
        uint32_t height,
        VkFormat format = VK_FORMAT_B8G8R8A8_UNORM
    );
    
    // Free a previously imported texture
    void FreeImportedTexture(ImportedVulkanTexture& texture);
    
    // Get the raw VkImage for Godot integration
    VkImage GetVkImage() const { return m_currentTexture.image; }
    
    // Check if we have a valid imported texture
    bool HasValidTexture() const { return m_currentTexture.valid; }

private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    
    // Extension function pointers (loaded dynamically)
    PFN_vkGetMemoryWin32HandlePropertiesKHR m_pfnGetMemoryWin32HandlePropertiesKHR = nullptr;
    
    // State
    bool m_initialized = false;
    bool m_externalMemorySupported = false;
    ImportedVulkanTexture m_currentTexture;
    
    // Find a suitable memory type for imported memory
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
};

} // namespace CefWebviewGodot

#endif // CEF_USE_VULKAN_INTEROP
