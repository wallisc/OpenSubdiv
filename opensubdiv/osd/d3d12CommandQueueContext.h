//
//   Copyright 2013 Pixar
//
//   Licensed under the Apache License, Version 2.0 (the "Apache License")
//   with the following modification; you may not use this file except in
//   compliance with the Apache License and the following modification to it:
//   Section 6. Trademarks. is deleted and replaced with:
//
//   6. Trademarks. This License does not grant permission to use the trade
//      names, trademarks, service marks, or product names of the Licensor
//      and its affiliates, except as required to comply with Section 4(c) of
//      the License and to reproduce the content of the NOTICE file.
//
//   You may obtain a copy of the Apache License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the Apache License with the above modification is
//   distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//   KIND, either express or implied. See the Apache License for the specific
//   language governing permissions and limitations under the Apache License.
//

#ifndef OPENSUBDIV3_OSD_D3D12_COMMON_H
#define OPENSUBDIV3_OSD_D3D12_COMMON_H

#include <atlbase.h>
#include <assert.h>
#include "d3d12FenceTrackedObjectQueue.h"
#include "d3d12PoolAllocator.h"

struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;
struct ID3D12CommandAllocator;
struct ID3D12Object;
struct ID3D11DeviceContext;
struct ID3D12Device;
struct ID3D12CommandList;
struct ID3D12Fence;

namespace OpenSubdiv {
namespace OPENSUBDIV_VERSION {

namespace Osd {
    class D3D12DeferredDeletionQueue : public FenceTrackedObjectQueue<ID3D12Object *>
    {
    public:
        void Push(unsigned long long fenceValue, ID3D12Object *object);
        void DeleteObject(ID3D12Object **object);
    };

    class CommandListAllocatorPair
    {
    public:
        ID3D12GraphicsCommandList *_commandList;
        ID3D12CommandAllocator *_allocator;
    };

    class CommandListAllocatorPairAllocator : public IAllocator<CommandListAllocatorPair>
    {
    public:
        CommandListAllocatorPairAllocator(ID3D12Device *device, unsigned int nodeMask);

        CommandListAllocatorPair Allocate();

        void Free(CommandListAllocatorPair &allocation);
        void Reset(CommandListAllocatorPair &allocation);
    private:
        ID3D12Device *_device;
        unsigned int _nodeMask;
    };

    class D3D12CommandQueueContext
    {
    public:
        D3D12CommandQueueContext(ID3D12CommandQueue *commandQueue, ID3D12Device *device, unsigned int nodeMask, ID3D11DeviceContext *D3D12CommandQueueContext11);
        ~D3D12CommandQueueContext();

        ID3D11DeviceContext *_D3D12CommandQueueContext11;

        unsigned int GetNodeMask() { return _nodeMask; }

        ID3D11DeviceContext *GetDeviceContext() { return _D3D12CommandQueueContext11; }

        CommandListAllocatorPair GetCommandListAllocatorPair();

        void ExecuteCommandList(ID3D12CommandList *CommandList);

        void Syncronize();

        void ReleaseCommandListAllocatorPair(CommandListAllocatorPair &pair);

        ID3D12Device *GetDevice() const {
            return _device;
        }

        ID3D12CommandQueue *GetCommandQueue() const {
            return _queue;
        }

        void DeleteD3D12Object(ID3D12Object *Object);
    private:
        void NotifyOnCommandListSubmission();

        D3D12DeferredDeletionQueue _deferredDeletionQueue;
        D3D12PoolAllocator<CommandListAllocatorPair, CommandListAllocatorPairAllocator> _commandListAllocator;
        CComPtr<ID3D12Device> _device;
        CComPtr<ID3D12CommandQueue> _queue;
        CComPtr<ID3D12Fence> _fence;
        unsigned long long _fenceValue;
        unsigned int _nodeMask;
        HANDLE _waitEvent;
    };

    typedef unsigned long long OSD_D3D12_GPU_VIRTUAL_ADDRESS;

    D3D12CommandQueueContext *CreateD3D12CommandQueueContext(ID3D12CommandQueue *pCommandQueue, unsigned int nodeMask, ID3D11DeviceContext *D3D12CommandQueueContext);
    void FreeD3D12CommandQueueContext(D3D12CommandQueueContext *D3D12CommandQueueContext);
}  // end namespace Osd

}  // end namespace OPENSUBDIV_VERSION
using namespace OPENSUBDIV_VERSION;

}  // end namespace OpenSubdiv

#endif  // OPENSUBDIV3_OSD_D3D12_COMMON_H
