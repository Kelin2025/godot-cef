#pragma once

#ifdef CEF_USE_D3D12_INTEROP

#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdint>

using Microsoft::WRL::ComPtr;

namespace CefWebviewGodot {

// Manages D3D11 shared texture import and copying to D3D12
// CEF provides a D3D11 shared texture handle. We:
// 1. Create a D3D11 device on the same adapter as Godot's D3D12 device
// 2. Open the shared handle with D3D11
// 3. Copy to a D3D11 staging texture that's shared with D3D12
// 4. Provide the D3D12 resource to Godot
class D3D12Interop {
public:
    D3D12Interop();
    ~D3D12Interop();

    // Initialize with D3D12 device from Godot
    bool Initialize(void* d3d12Device);
    
    // Import a D3D11 shared handle into D3D12
    // Returns true if the texture was successfully imported and is ready
    bool ImportSharedTexture(
        HANDLE sharedHandle,
        uint32_t width,
        uint32_t height
    );
    
    // Get the D3D12 resource for Godot's texture_create_from_extension
    ID3D12Resource* GetD3D12Resource() const { return m_d3d12Resource.Get(); }
    
    // Check if we have a valid imported texture
    bool HasValidTexture() const { return m_d3d12Resource != nullptr; }
    
    // Get resource as uint64 for Godot's texture_create_from_extension
    uint64_t GetResourceHandle() const { 
        return reinterpret_cast<uint64_t>(m_d3d12Resource.Get()); 
    }
    
    // NEW: Copy our shared texture TO a Godot-owned D3D12 texture
    // This avoids the texture_create_from_extension rejection
    bool CopyToGodotTexture(ID3D12Resource* godotTexture, ID3D12CommandQueue* cmdQueue);
    
    // NEW: Setup D3D12 command list for copying (one-time init)
    bool SetupD3D12CopyResources(ID3D12CommandQueue* cmdQueue);

private:
    bool CreateD3D11Device(IDXGIAdapter* adapter);
    bool CreateSharedD3D12Texture(uint32_t width, uint32_t height);
    
    // D3D12 side (Godot's device)
    ComPtr<ID3D12Device> m_d3d12Device;
    ComPtr<ID3D12Resource> m_d3d12Resource;  // The final texture Godot will use
    
    // D3D11 side (our device for opening CEF's shared handle)
    ComPtr<ID3D11Device> m_d3d11Device;
    ComPtr<ID3D11DeviceContext> m_d3d11Context;
    ComPtr<ID3D11Device1> m_d3d11Device1;  // For OpenSharedResource1
    
    // Shared texture (created by D3D11, opened by D3D12)
    ComPtr<ID3D11Texture2D> m_d3d11SharedTexture;  // D3D11 side of the shared texture
    HANDLE m_sharedNTHandle = nullptr;  // NT handle for cross-API sharing
    
    HANDLE m_lastCefHandle = nullptr;
    uint32_t m_currentWidth = 0;
    uint32_t m_currentHeight = 0;
    bool m_initialized = false;
    
    // D3D12 copy resources
    ComPtr<ID3D12CommandAllocator> m_cmdAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_cmdList;
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    uint64_t m_fenceValue = 0;
    bool m_copyResourcesInitialized = false;
};

} // namespace CefWebviewGodot

#endif // CEF_USE_D3D12_INTEROP
