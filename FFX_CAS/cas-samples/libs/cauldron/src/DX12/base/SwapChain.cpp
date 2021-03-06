// AMD AMDUtils code
// 
// Copyright(c) 2018 Advanced Micro Devices, Inc.All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "stdafx.h"
#include "Misc\Misc.h"
#include "base\Device.h"
#include "Helper.h"

#include "SwapChain.h"

#pragma comment(lib, "dxgi.lib")

namespace CAULDRON_DX12
{
    DXGI_FORMAT SwapChain::GetFormat()
    {
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    };

    //--------------------------------------------------------------------------------------
    //
    // OnCreate
    //
    //--------------------------------------------------------------------------------------
    void SwapChain::OnCreate(Device *pDevice, uint32_t numberBackBuffers, HWND hWnd)
    {
        m_hWnd = hWnd;
        m_pDevice = pDevice->GetDevice();
        m_pDirectQueue = pDevice->GetGraphicsQueue();
        m_BackBufferCount = numberBackBuffers;

        CreateDXGIFactory1(IID_PPV_ARGS(&m_pFactory));

        // Describe the swap chain.
        m_descSwapChain = {};
        m_descSwapChain.BufferCount = m_BackBufferCount;
        m_descSwapChain.Width = 0;
        m_descSwapChain.Height = 0;
        m_descSwapChain.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        m_descSwapChain.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        m_descSwapChain.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        m_descSwapChain.SampleDesc.Count = 1;
        m_descSwapChain.Flags = 0;

        // It is recommended to always use the tearing flag when it is available.
        //descSwapChain.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        m_Fences.resize(m_descSwapChain.BufferCount);
        for (uint32_t n = 0; n < m_descSwapChain.BufferCount; n++)
        {
            // This fence is for the swap chain synchronization
            m_Fences[n].OnCreate(pDevice, format("swapchain fence %i",n).c_str());
        }

        IDXGISwapChain1 *pSwapChain;
        ThrowIfFailed(m_pFactory->CreateSwapChainForHwnd(
            m_pDirectQueue,        // Swap chain needs the queue so that it can force a flush on it.
            m_hWnd,
            &m_descSwapChain,
            nullptr,
            nullptr,
            &pSwapChain
        ));

        ThrowIfFailed(m_pFactory->MakeWindowAssociation(m_hWnd, DXGI_MWA_NO_ALT_ENTER));

        ThrowIfFailed(pSwapChain->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&m_pSwapChain));
        pSwapChain->Release();

        //
        // create RTV heaps
        //
        D3D12_DESCRIPTOR_HEAP_DESC descHeapRtv;
        descHeapRtv.NumDescriptors = m_descSwapChain.BufferCount;
        descHeapRtv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        descHeapRtv.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        descHeapRtv.NodeMask = 0;
        ThrowIfFailed(m_pDevice->CreateDescriptorHeap(&descHeapRtv, IID_PPV_ARGS(&m_RTVHeaps)));

        CreateRTV();
    }

    //--------------------------------------------------------------------------------------
    //
    // OnDestroy
    //
    //--------------------------------------------------------------------------------------
    void SwapChain::OnDestroy()
    {
        for (uint32_t n = 0; n < m_descSwapChain.BufferCount; n++)
            m_Fences[n].OnDestroy();

        m_RTVHeaps->Release();

        m_pSwapChain->Release();
        m_pFactory->Release();
    }

    ID3D12Resource *SwapChain::GetCurrentBackBufferResource()
    {
        uint32_t backBuffferIndex = m_pSwapChain->GetCurrentBackBufferIndex();

        ID3D12Resource *pBackBuffer;
        ThrowIfFailed(m_pSwapChain->GetBuffer(backBuffferIndex, IID_PPV_ARGS(&pBackBuffer)));
        pBackBuffer->Release();
        return pBackBuffer;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE *SwapChain::GetCurrentBackBufferRTV()
    {
        uint32_t backBuffferIndex = m_pSwapChain->GetCurrentBackBufferIndex();
        return &m_CPUView[backBuffferIndex];
    }

    void SwapChain::WaitForSwapChain()
    {
        uint32_t backBuffferIndex = m_pSwapChain->GetCurrentBackBufferIndex();
        m_Fences[(backBuffferIndex) % m_BackBufferCount].CpuWaitForFence(0);
    };

    void SwapChain::Present()
    {
        ThrowIfFailed(m_pSwapChain->Present(0, 0));

        // issue a fence so we can tell when this frame ended
        uint32_t backBuffferIndex = m_pSwapChain->GetCurrentBackBufferIndex();
        m_Fences[backBuffferIndex].IssueFence(m_pDirectQueue);
    }


    void SwapChain::SetFullScreen(bool fullscreen)
    {
        ThrowIfFailed(m_pSwapChain->SetFullscreenState(fullscreen, nullptr));
    }


    void SwapChain::OnCreateWindowSizeDependentResources(uint32_t dwWidth, uint32_t dwHeight)
    {
        // Set up the swap chain to allow back buffers to live on multiple GPU nodes.
        ThrowIfFailed(
            m_pSwapChain->ResizeBuffers(
                m_descSwapChain.BufferCount,
                dwWidth,
                dwHeight,
                DXGI_FORMAT_R8G8B8A8_UNORM,
                m_descSwapChain.Flags)
        );

        CreateRTV();
    }

    void SwapChain::OnDestroyWindowSizeDependentResources()
    {
    }

    void SwapChain::CreateRTV()
    {
        //
        // create RTV's
        //
        uint32_t colorDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_CPUView.resize(m_descSwapChain.BufferCount);
        for (uint32_t i = 0; i < m_descSwapChain.BufferCount; i++)
        {
            m_CPUView[i] = m_RTVHeaps->GetCPUDescriptorHandleForHeapStart();
            m_CPUView[i].ptr += colorDescriptorSize * i;

            ID3D12Resource *pBackBuffer;
            ThrowIfFailed(m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer)));
            SetName(pBackBuffer, "SwapChain");

            D3D12_RESOURCE_DESC desc = pBackBuffer->GetDesc();

            D3D12_RENDER_TARGET_VIEW_DESC colorDesc = {};
            colorDesc.Format = GetFormat();
            colorDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            colorDesc.Texture2D.MipSlice = 0;
            colorDesc.Texture2D.PlaneSlice = 0;

            m_pDevice->CreateRenderTargetView(pBackBuffer, &colorDesc, m_CPUView[i]);
            SetName(pBackBuffer, format("BackBuffer %i", i));
            pBackBuffer->Release();
        }
    }
}