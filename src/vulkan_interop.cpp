#ifdef CEF_USE_VULKAN_INTEROP

#include "vulkan_interop.h"
#include "cef_debug.h"
#include <vector>
#include <cstring>

namespace CefWebviewGodot {

VulkanInterop::VulkanInterop() = default;

VulkanInterop::~VulkanInterop() {
    if (m_currentTexture.valid) {
        FreeImportedTexture(m_currentTexture);
    }
}

bool VulkanInterop::Initialize(
    VkInstance instance,
    VkPhysicalDevice physicalDevice,
    VkDevice device
) {
    if (m_initialized) {
        return true;
    }
    
    m_instance = instance;
    m_physicalDevice = physicalDevice;
    m_device = device;
    
    if (!m_instance || !m_physicalDevice || !m_device) {
        CEF_DEBUG_PRINT("[CEF Vulkan] Invalid Vulkan handles provided");
        return false;
    }
    
    // Check if the physical device supports external memory
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, nullptr);
    
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, availableExtensions.data());
    
    bool hasExternalMemory = false;
    bool hasExternalMemoryWin32 = false;
    for (const auto& ext : availableExtensions) {
        if (strcmp(ext.extensionName, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME) == 0) {
            hasExternalMemory = true;
        }
        if (strcmp(ext.extensionName, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME) == 0) {
            hasExternalMemoryWin32 = true;
        }
    }
    
    CEF_DEBUG_PRINT("[CEF Vulkan] Device supports VK_KHR_external_memory: ", hasExternalMemory ? "yes" : "no");
    CEF_DEBUG_PRINT("[CEF Vulkan] Device supports VK_KHR_external_memory_win32: ", hasExternalMemoryWin32 ? "yes" : "no");
    
    if (!hasExternalMemoryWin32) {
        CEF_DEBUG_PRINT("[CEF Vulkan] GPU does not support external memory import");
        m_externalMemorySupported = false;
        return false;
    }
    
    // Load extension function for importing Win32 handles
    // This will only work if Godot enabled the extension when creating the VkDevice
    m_pfnGetMemoryWin32HandlePropertiesKHR = reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(
        vkGetDeviceProcAddr(m_device, "vkGetMemoryWin32HandlePropertiesKHR")
    );
    
    if (!m_pfnGetMemoryWin32HandlePropertiesKHR) {
        CEF_DEBUG_PRINT("[CEF Vulkan] VK_KHR_external_memory_win32 extension is supported by GPU but NOT enabled in Godot's VkDevice");
        CEF_DEBUG_PRINT("[CEF Vulkan] This requires Godot to be compiled with external memory support enabled");
        CEF_DEBUG_PRINT("[CEF Vulkan] Falling back to CPU texture copy path");
        m_externalMemorySupported = false;
        return false;
    }
    
    m_externalMemorySupported = true;
    m_initialized = true;
    CEF_DEBUG_PRINT("[CEF Vulkan] Initialized with external memory support");
    return true;
}

uint32_t VulkanInterop::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);
    
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    return UINT32_MAX;
}

ImportedVulkanTexture VulkanInterop::ImportSharedTexture(
    HANDLE sharedHandle,
    uint32_t width,
    uint32_t height,
    VkFormat format
) {
    ImportedVulkanTexture result;
    result.width = width;
    result.height = height;
    result.valid = false;
    
    if (!m_initialized || !m_externalMemorySupported) {
        CEF_DEBUG_PRINT("[CEF Vulkan] Cannot import - not initialized");
        return result;
    }
    
    if (!sharedHandle) {
        CEF_DEBUG_PRINT("[CEF Vulkan] Invalid shared handle");
        return result;
    }
    
    // Query the memory properties of the shared handle
    // CEF provides an NTHANDLE (D3D11_RESOURCE_MISC_SHARED_NTHANDLE), try different handle types
    VkMemoryWin32HandlePropertiesKHR handleProps = {};
    handleProps.sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR;
    
    // Try different handle types - CEF's NTHANDLE may work with different types
    VkExternalMemoryHandleTypeFlagBits handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    VkResult vkResult = m_pfnGetMemoryWin32HandlePropertiesKHR(
        m_device,
        handleType,
        sharedHandle,
        &handleProps
    );
    
    if (vkResult != VK_SUCCESS) {
        // Try D3D11 texture type
        handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
        vkResult = m_pfnGetMemoryWin32HandlePropertiesKHR(
            m_device,
            handleType,
            sharedHandle,
            &handleProps
        );
    }
    
    if (vkResult != VK_SUCCESS) {
        // Try D3D11 texture KMT type
        handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT;
        vkResult = m_pfnGetMemoryWin32HandlePropertiesKHR(
            m_device,
            handleType,
            sharedHandle,
            &handleProps
        );
    }
    
    if (vkResult != VK_SUCCESS) {
        static int errorCount = 0;
        if (++errorCount <= 3) {
            CEF_DEBUG_PRINT("[CEF Vulkan] Cannot import D3D11 NTHANDLE into Vulkan");
            CEF_DEBUG_PRINT("[CEF Vulkan] CEF's shared texture handle is not Vulkan-compatible");
            CEF_DEBUG_PRINT("[CEF Vulkan] D3D11 NTHANDLE can only be shared with D3D11/D3D12, not Vulkan");
            CEF_DEBUG_PRINT("[CEF Vulkan] Falling back to CPU path");
        }
        return result;
    }
    
    CEF_DEBUG_PRINT("[CEF Vulkan] Handle accepted with type: ", (int)handleType);
    
    // Create a VkImage that will use external memory
    VkExternalMemoryImageCreateInfo externalMemoryImageInfo = {};
    externalMemoryImageInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    externalMemoryImageInfo.handleTypes = handleType;
    
    VkImageCreateInfo imageCreateInfo = {};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.pNext = &externalMemoryImageInfo;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = format;
    imageCreateInfo.extent.width = width;
    imageCreateInfo.extent.height = height;
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    vkResult = vkCreateImage(m_device, &imageCreateInfo, nullptr, &result.image);
    if (vkResult != VK_SUCCESS) {
        CEF_DEBUG_PRINT("[CEF Vulkan] Failed to create image: ", (int)vkResult);
        return result;
    }
    
    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, result.image, &memRequirements);
    
    // Find compatible memory type from handle properties
    uint32_t memoryTypeIndex = FindMemoryType(
        handleProps.memoryTypeBits & memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    if (memoryTypeIndex == UINT32_MAX) {
        CEF_DEBUG_PRINT("[CEF Vulkan] No compatible memory type found");
        vkDestroyImage(m_device, result.image, nullptr);
        result.image = VK_NULL_HANDLE;
        return result;
    }
    
    // Import the D3D11 shared handle as Vulkan memory
    VkImportMemoryWin32HandleInfoKHR importInfo = {};
    importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
    importInfo.handleType = handleType;
    importInfo.handle = sharedHandle;
    
    // For D3D11 textures, we need to use dedicated allocation
    VkMemoryDedicatedAllocateInfo dedicatedInfo = {};
    dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicatedInfo.pNext = &importInfo;
    dedicatedInfo.image = result.image;
    
    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &dedicatedInfo;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    
    vkResult = vkAllocateMemory(m_device, &allocInfo, nullptr, &result.memory);
    if (vkResult != VK_SUCCESS) {
        CEF_DEBUG_PRINT("[CEF Vulkan] Failed to allocate memory: ", (int)vkResult);
        vkDestroyImage(m_device, result.image, nullptr);
        result.image = VK_NULL_HANDLE;
        return result;
    }
    
    // Bind the imported memory to the image
    vkResult = vkBindImageMemory(m_device, result.image, result.memory, 0);
    if (vkResult != VK_SUCCESS) {
        CEF_DEBUG_PRINT("[CEF Vulkan] Failed to bind image memory: ", (int)vkResult);
        vkFreeMemory(m_device, result.memory, nullptr);
        vkDestroyImage(m_device, result.image, nullptr);
        result.image = VK_NULL_HANDLE;
        result.memory = VK_NULL_HANDLE;
        return result;
    }
    
    // Create an image view for sampling
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = result.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    vkResult = vkCreateImageView(m_device, &viewInfo, nullptr, &result.imageView);
    if (vkResult != VK_SUCCESS) {
        CEF_DEBUG_PRINT("[CEF Vulkan] Failed to create image view: ", (int)vkResult);
        vkFreeMemory(m_device, result.memory, nullptr);
        vkDestroyImage(m_device, result.image, nullptr);
        result.image = VK_NULL_HANDLE;
        result.memory = VK_NULL_HANDLE;
        return result;
    }
    
    result.valid = true;
    CEF_DEBUG_PRINT("[CEF Vulkan] Successfully imported shared texture ", width, "x", height);
    return result;
}

void VulkanInterop::FreeImportedTexture(ImportedVulkanTexture& texture) {
    if (!m_device) return;
    
    if (texture.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, texture.imageView, nullptr);
        texture.imageView = VK_NULL_HANDLE;
    }
    
    if (texture.image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, texture.image, nullptr);
        texture.image = VK_NULL_HANDLE;
    }
    
    if (texture.memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, texture.memory, nullptr);
        texture.memory = VK_NULL_HANDLE;
    }
    
    texture.valid = false;
}

} // namespace CefWebviewGodot

#endif // CEF_USE_VULKAN_INTEROP
