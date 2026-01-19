#ifdef CEF_USE_D3D12_INTEROP

#include "d3d12_interop.h"
#include "cef_debug.h"

namespace CefWebviewGodot {

D3D12Interop::D3D12Interop() = default;

D3D12Interop::~D3D12Interop() {
    // Close the NT handle if we created one
    if (m_sharedNTHandle) {
        CloseHandle(m_sharedNTHandle);
        m_sharedNTHandle = nullptr;
    }
    
    m_d3d12Resource.Reset();
    m_d3d11SharedTexture.Reset();
    m_d3d11Context.Reset();
    m_d3d11Device1.Reset();
    m_d3d11Device.Reset();
    m_d3d12Device.Reset();
}

bool D3D12Interop::Initialize(void* d3d12Device) {
    if (m_initialized) {
        return true;
    }
    
    if (!d3d12Device) {
        CEF_DEBUG_PRINT("[CEF D3D12] Invalid D3D12 device provided");
        return false;
    }
    
    // Store D3D12 device
    m_d3d12Device = static_cast<ID3D12Device*>(d3d12Device);
    
    // Get the DXGI adapter from D3D12 device so we can create a D3D11 device on the same GPU
    LUID adapterLuid = m_d3d12Device->GetAdapterLuid();
    CEF_DEBUG_PRINT("[CEF D3D12] Godot D3D12 adapter LUID: ", 
        (int)adapterLuid.LowPart, "/", (int)adapterLuid.HighPart);
    
    ComPtr<IDXGIFactory4> dxgiFactory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) {
        CEF_DEBUG_PRINT("[CEF D3D12] Failed to create DXGI factory: ", (int)hr);
        return false;
    }
    
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiFactory->EnumAdapterByLuid(adapterLuid, IID_PPV_ARGS(&adapter));
    if (FAILED(hr)) {
        CEF_DEBUG_PRINT("[CEF D3D12] Failed to get adapter by LUID: ", (int)hr);
        return false;
    }
    
    // Log adapter info
    DXGI_ADAPTER_DESC adapterDesc;
    if (SUCCEEDED(adapter->GetDesc(&adapterDesc))) {
        // Convert wide string to narrow for logging
        char adapterName[128];
        wcstombs(adapterName, adapterDesc.Description, 128);
        CEF_DEBUG_PRINT("[CEF D3D12] Using adapter: ", adapterName);
    }
    
    // Create D3D11 device on the same adapter
    if (!CreateD3D11Device(adapter.Get())) {
        return false;
    }
    
    m_initialized = true;
    CEF_DEBUG_PRINT("[CEF D3D12] Initialized with D3D12 device");
    return true;
}

bool D3D12Interop::CreateD3D11Device(IDXGIAdapter* adapter) {
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    
    D3D_FEATURE_LEVEL actualFeatureLevel;
    HRESULT hr = D3D11CreateDevice(
        adapter,
        D3D_DRIVER_TYPE_UNKNOWN,  // Must use UNKNOWN when specifying adapter
        nullptr,
        createFlags,
        featureLevels,
        _countof(featureLevels),
        D3D11_SDK_VERSION,
        &m_d3d11Device,
        &actualFeatureLevel,
        &m_d3d11Context
    );
    
    if (FAILED(hr)) {
        CEF_DEBUG_PRINT("[CEF D3D12] Failed to create D3D11 device: ", (int)hr);
        return false;
    }
    
    CEF_DEBUG_PRINT("[CEF D3D12] D3D11 device created, feature level: ", 
        (actualFeatureLevel == D3D_FEATURE_LEVEL_11_1) ? "11.1" : "11.0");
    
    // Get ID3D11Device1 for OpenSharedResource1
    hr = m_d3d11Device.As(&m_d3d11Device1);
    if (FAILED(hr)) {
        CEF_DEBUG_PRINT("[CEF D3D12] Failed to get ID3D11Device1: ", (int)hr);
        return false;
    }
    
    // Verify D3D11Device1 is valid by getting its immediate context
    ComPtr<ID3D11DeviceContext1> ctx1;
    m_d3d11Device1->GetImmediateContext1(&ctx1);
    if (!ctx1) {
        CEF_DEBUG_PRINT("[CEF D3D12] Warning: Could not get ID3D11DeviceContext1");
    } else {
        CEF_DEBUG_PRINT("[CEF D3D12] ID3D11Device1 validated successfully");
    }
    
    // Get DXGI device to check adapter LUID
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_d3d11Device.As(&dxgiDevice);
    if (SUCCEEDED(hr)) {
        ComPtr<IDXGIAdapter> d3d11Adapter;
        hr = dxgiDevice->GetAdapter(&d3d11Adapter);
        if (SUCCEEDED(hr)) {
            DXGI_ADAPTER_DESC desc;
            d3d11Adapter->GetDesc(&desc);
            CEF_DEBUG_PRINT("[CEF D3D12] D3D11 adapter LUID: ", 
                (int)desc.AdapterLuid.LowPart, "/", (int)desc.AdapterLuid.HighPart);
        }
    }
    
    // Self-test: Create a shared texture and try to open it
    // This verifies our D3D11 device can open shared resources
    {
        D3D11_TEXTURE2D_DESC testDesc = {};
        testDesc.Width = 64;
        testDesc.Height = 64;
        testDesc.MipLevels = 1;
        testDesc.ArraySize = 1;
        testDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        testDesc.SampleDesc.Count = 1;
        testDesc.Usage = D3D11_USAGE_DEFAULT;
        testDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        testDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
        
        ComPtr<ID3D11Texture2D> testTex;
        hr = m_d3d11Device->CreateTexture2D(&testDesc, nullptr, &testTex);
        if (SUCCEEDED(hr)) {
            ComPtr<IDXGIResource1> dxgiRes;
            hr = testTex.As(&dxgiRes);
            if (SUCCEEDED(hr)) {
                HANDLE testHandle = nullptr;
                hr = dxgiRes->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &testHandle);
                if (SUCCEEDED(hr) && testHandle) {
                    // Try to open our own handle
                    ComPtr<ID3D11Texture2D> reopened;
                    HRESULT openHr = m_d3d11Device1->OpenSharedResource1(testHandle, IID_PPV_ARGS(&reopened));
                    if (SUCCEEDED(openHr)) {
                        CEF_DEBUG_PRINT("[CEF D3D12] Self-test PASSED: Can open our own shared NT handles");
                    } else {
                        CEF_DEBUG_PRINT("[CEF D3D12] Self-test FAILED: Cannot open our own handle: ", (int)openHr);
                    }
                    CloseHandle(testHandle);
                } else {
                    CEF_DEBUG_PRINT("[CEF D3D12] Self-test: Failed to create shared handle: ", (int)hr);
                }
            }
        } else {
            CEF_DEBUG_PRINT("[CEF D3D12] Self-test: Failed to create test texture: ", (int)hr);
        }
    }
    
    CEF_DEBUG_PRINT("[CEF D3D12] Created D3D11 device on same adapter");
    return true;
}

bool D3D12Interop::CreateSharedD3D12Texture(uint32_t width, uint32_t height) {
    // Create a D3D11 texture with SHARED_NTHANDLE so D3D12 can open it
    // Note: Don't use SHARED_KEYEDMUTEX - it complicates D3D12 interop
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
    
    HRESULT hr = m_d3d11Device->CreateTexture2D(&desc, nullptr, &m_d3d11SharedTexture);
    if (FAILED(hr)) {
        CEF_DEBUG_PRINT("[CEF D3D12] Failed to create D3D11 shared texture: ", (int)hr);
        return false;
    }
    
    // Get the shared NT handle
    ComPtr<IDXGIResource1> dxgiResource;
    hr = m_d3d11SharedTexture.As(&dxgiResource);
    if (FAILED(hr)) {
        CEF_DEBUG_PRINT("[CEF D3D12] Failed to get IDXGIResource1: ", (int)hr);
        return false;
    }
    
    // Close old handle if it exists
    if (m_sharedNTHandle) {
        CloseHandle(m_sharedNTHandle);
        m_sharedNTHandle = nullptr;
    }
    
    hr = dxgiResource->CreateSharedHandle(
        nullptr,  // Security attributes
        DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
        nullptr,  // Name
        &m_sharedNTHandle
    );
    if (FAILED(hr)) {
        CEF_DEBUG_PRINT("[CEF D3D12] Failed to create shared NT handle: ", (int)hr);
        return false;
    }
    
    // Open the shared handle with D3D12
    m_d3d12Resource.Reset();
    hr = m_d3d12Device->OpenSharedHandle(m_sharedNTHandle, IID_PPV_ARGS(&m_d3d12Resource));
    if (FAILED(hr)) {
        CEF_DEBUG_PRINT("[CEF D3D12] Failed to open shared handle in D3D12: ", (int)hr);
        return false;
    }
    
    // Verify D3D12 resource
    D3D12_RESOURCE_DESC resDesc = m_d3d12Resource->GetDesc();
    CEF_DEBUG_PRINT("[CEF D3D12] D3D12 resource created: ", 
        (int)resDesc.Width, "x", (int)resDesc.Height, " format=", (int)resDesc.Format);
    
    m_currentWidth = width;
    m_currentHeight = height;
    
    CEF_DEBUG_PRINT("[CEF D3D12] Created shared D3D11<->D3D12 texture: ", (int)width, "x", (int)height);
    return true;
}

bool D3D12Interop::ImportSharedTexture(
    HANDLE sharedHandle,
    uint32_t width,
    uint32_t height
) {
    if (!m_initialized || !m_d3d12Device) {
        CEF_DEBUG_PRINT("[CEF D3D12] Cannot import - not initialized");
        return false;
    }
    
    if (!sharedHandle) {
        CEF_DEBUG_PRINT("[CEF D3D12] Invalid shared handle");
        return false;
    }
    
    static int attemptCount = 0;
    attemptCount++;
    
    // We need to COPY the texture because CEF releases it back to the pool after the callback.
    // Direct D3D12 import would give us CEF's resource, but we can't keep it.
    // Instead, use D3D11 to open CEF's texture and copy to our own shared texture.
    
    if (!m_d3d11Device1) {
        if (attemptCount <= 5) {
            CEF_DEBUG_PRINT("[CEF D3D12] No D3D11 device available for texture copy");
        }
        return false;
    }
    
    // Recreate our shared texture if size changed
    if (!m_d3d11SharedTexture || width != m_currentWidth || height != m_currentHeight) {
        if (!CreateSharedD3D12Texture(width, height)) {
            return false;
        }
    }
    
    // Try OpenSharedResource1 (NT handles)
    ComPtr<ID3D11Texture2D> cefTexture;
    HRESULT hrNT = m_d3d11Device1->OpenSharedResource1(
        sharedHandle,
        IID_PPV_ARGS(&cefTexture)
    );
    
    HRESULT hrLegacy = E_FAIL;
    if (FAILED(hrNT)) {
        // Try legacy OpenSharedResource (non-NT handles)
        hrLegacy = m_d3d11Device->OpenSharedResource(
            sharedHandle,
            IID_PPV_ARGS(&cefTexture)
        );
    }
    
    HRESULT hr = SUCCEEDED(hrNT) ? hrNT : hrLegacy;
    
    if (FAILED(hr)) {
        if (attemptCount <= 5 || attemptCount % 60 == 1) {
            CEF_DEBUG_PRINT("[CEF D3D12] D3D11 open failed #", attemptCount,
                " NT=", (int)hrNT, " Legacy=", (int)hrLegacy,
                " handle=", (uint64_t)sharedHandle);
        }
        return false;
    }
    
    // D3D11 worked - copy to our shared texture
    if (attemptCount <= 5) {
        D3D11_TEXTURE2D_DESC texDesc;
        cefTexture->GetDesc(&texDesc);
        CEF_DEBUG_PRINT("[CEF D3D12] D3D11 opened CEF texture #", attemptCount, ": ", 
            (int)texDesc.Width, "x", (int)texDesc.Height, " format=", (int)texDesc.Format,
            " MiscFlags=", (int)texDesc.MiscFlags);
    }
    
    // Copy from CEF's texture to our shared texture
    m_d3d11Context->CopyResource(m_d3d11SharedTexture.Get(), cefTexture.Get());
    m_d3d11Context->Flush();
    
    m_lastCefHandle = sharedHandle;
    
    if (attemptCount % 60 == 1) {
        CEF_DEBUG_PRINT("[CEF D3D12] Frame copied successfully, count=", attemptCount);
    }
    
    return true;
}

bool D3D12Interop::SetupD3D12CopyResources(ID3D12CommandQueue* cmdQueue) {
    if (m_copyResourcesInitialized) {
        return true;
    }
    
    if (!m_d3d12Device || !cmdQueue) {
        CEF_DEBUG_PRINT("[CEF D3D12] Cannot setup copy resources - missing device or queue");
        return false;
    }
    
    // Create command allocator
    HRESULT hr = m_d3d12Device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&m_cmdAllocator)
    );
    if (FAILED(hr)) {
        CEF_DEBUG_PRINT("[CEF D3D12] Failed to create command allocator: ", (int)hr);
        return false;
    }
    
    // Create command list
    hr = m_d3d12Device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_cmdAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&m_cmdList)
    );
    if (FAILED(hr)) {
        CEF_DEBUG_PRINT("[CEF D3D12] Failed to create command list: ", (int)hr);
        return false;
    }
    
    // Close the command list initially (we'll reset it when needed)
    m_cmdList->Close();
    
    // Create fence for synchronization
    hr = m_d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr)) {
        CEF_DEBUG_PRINT("[CEF D3D12] Failed to create fence: ", (int)hr);
        return false;
    }
    
    // Create fence event
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) {
        CEF_DEBUG_PRINT("[CEF D3D12] Failed to create fence event");
        return false;
    }
    
    m_fenceValue = 0;
    m_copyResourcesInitialized = true;
    CEF_DEBUG_PRINT("[CEF D3D12] Copy resources initialized");
    return true;
}

bool D3D12Interop::CopyToGodotTexture(ID3D12Resource* godotTexture, ID3D12CommandQueue* cmdQueue) {
    if (!m_d3d12Resource || !godotTexture || !cmdQueue) {
        return false;
    }
    
    // Setup copy resources if not done yet
    if (!m_copyResourcesInitialized) {
        if (!SetupD3D12CopyResources(cmdQueue)) {
            return false;
        }
    }
    
    HRESULT hr;
    
    // Reset command allocator and command list
    hr = m_cmdAllocator->Reset();
    if (FAILED(hr)) {
        CEF_DEBUG_PRINT("[CEF D3D12] Failed to reset command allocator: ", (int)hr);
        return false;
    }
    
    hr = m_cmdList->Reset(m_cmdAllocator.Get(), nullptr);
    if (FAILED(hr)) {
        CEF_DEBUG_PRINT("[CEF D3D12] Failed to reset command list: ", (int)hr);
        return false;
    }
    
    // Transition Godot's texture to COPY_DEST state
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = godotTexture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;  // Godot's default
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    m_cmdList->ResourceBarrier(1, &barrier);
    
    // Copy from our shared texture to Godot's texture
    m_cmdList->CopyResource(godotTexture, m_d3d12Resource.Get());
    
    // Transition Godot's texture back to shader resource state
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_cmdList->ResourceBarrier(1, &barrier);
    
    // Close and execute
    hr = m_cmdList->Close();
    if (FAILED(hr)) {
        CEF_DEBUG_PRINT("[CEF D3D12] Failed to close command list: ", (int)hr);
        return false;
    }
    
    ID3D12CommandList* cmdLists[] = { m_cmdList.Get() };
    cmdQueue->ExecuteCommandLists(1, cmdLists);
    
    // Signal and wait for completion
    m_fenceValue++;
    hr = cmdQueue->Signal(m_fence.Get(), m_fenceValue);
    if (FAILED(hr)) {
        CEF_DEBUG_PRINT("[CEF D3D12] Failed to signal fence: ", (int)hr);
        return false;
    }
    
    if (m_fence->GetCompletedValue() < m_fenceValue) {
        hr = m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
        if (SUCCEEDED(hr)) {
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }
    
    return true;
}

} // namespace CefWebviewGodot

#endif // CEF_USE_D3D12_INTEROP
