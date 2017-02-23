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

#include "../osd/d3d12VertexBuffer.h"
#include "../far/error.h"

#include <d3d12.h>
#include <d3d11_1.h>
#include "d3dx12.h"
#include <cassert>

#include "d3d12util.h"

namespace OpenSubdiv {
namespace OPENSUBDIV_VERSION {

namespace Osd {

#define SAFE_RELEASE(p) { if(p) { (p)->Release(); (p)=NULL; } }

D3D12VertexBuffer::D3D12VertexBuffer(int numElements, int numVertices)
    : _numElements(numElements), _numVertices(numVertices) {
}

D3D12VertexBuffer::~D3D12VertexBuffer() {
}

D3D12VertexBuffer*
D3D12VertexBuffer::Create(int numElements, int numVertices,
                          D3D12CommandQueueContext* D3D12CommandQueueContext) {
    D3D12VertexBuffer *instance =
        new D3D12VertexBuffer(numElements, numVertices);

    if (instance->allocate(D3D12CommandQueueContext)) return instance;
    delete instance;
    return NULL;
}

void
D3D12VertexBuffer::UpdateData(const float *src, int startVertex, int numVertices,
                              D3D12CommandQueueContext *D3D12CommandQueueContext) {
    ID3D12CommandQueue *pCommandQueue = D3D12CommandQueueContext->GetCommandQueue();
    
    const unsigned int startOffset = startVertex * numVertices * sizeof(float);
    const unsigned int size = GetNumElements() * numVertices * sizeof(float);
    void *pData;
    ThrowFailure(_uploadBuffer->Map(0, nullptr, &pData));

    memcpy((BYTE *)pData + startOffset, src, size);

    D3D12_RANGE writtenRange = CD3DX12_RANGE(startOffset, size);
    _uploadBuffer->Unmap(0, &writtenRange);

    ScopedCommandListAllocatorPair pair(D3D12CommandQueueContext, D3D12CommandQueueContext->GetCommandListAllocatorPair());
    ID3D12GraphicsCommandList *pCommandList = pair._commandList;

    pCommandList->CopyBufferRegion(_buffer, 0, _uploadBuffer, startOffset, size);

    pCommandList->Close();
    
    D3D12CommandQueueContext->ExecuteCommandList(pCommandList);
}

int
D3D12VertexBuffer::GetNumElements() const {

    return _numElements;
}

int
D3D12VertexBuffer::GetNumVertices() const {

    return _numVertices;
}

CPUDescriptorHandle
D3D12VertexBuffer::BindD3D12Buffer(D3D12CommandQueueContext* D3D12CommandQueueContext) {

    return _uav;
}

CPUDescriptorHandle
D3D12VertexBuffer::BindD3D12UAV(D3D12CommandQueueContext* D3D12CommandQueueContext) {

    return BindD3D12Buffer(D3D12CommandQueueContext);
}

ID3D11Buffer *D3D12VertexBuffer::BindVBO(D3D12CommandQueueContext *D3D12CommandQueueContext)
{
    ScopedCommandListAllocatorPair pair(D3D12CommandQueueContext, D3D12CommandQueueContext->GetCommandListAllocatorPair());
    ID3D12GraphicsCommandList *pCommandList = pair._commandList;
    
    pCommandList->CopyBufferRegion(_readbackBuffer, 0, _buffer, 0, _dataSize);
    pCommandList->Close();

    D3D12CommandQueueContext->ExecuteCommandList(pCommandList);
    D3D12CommandQueueContext->Syncronize();

    void *pReadbackData;
    D3D12_RANGE readRange = { 0, (SIZE_T)_dataSize };
    _readbackBuffer->Map(0, &readRange, &pReadbackData);

    D3D11_MAPPED_SUBRESOURCE mappedSubresource;
    D3D12CommandQueueContext->GetDeviceContext()->Map(_d3d11Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
    memcpy(mappedSubresource.pData, pReadbackData, _dataSize);

    D3D12CommandQueueContext->GetDeviceContext()->Unmap(_d3d11Buffer, 0);
    _readbackBuffer->Unmap(0, nullptr);

    return _d3d11Buffer;
}


bool
D3D12VertexBuffer::allocate(D3D12CommandQueueContext* D3D12CommandQueueContext) {

    ID3D12Device *pDevice = D3D12CommandQueueContext->GetDevice();
    _dataSize = _numElements * _numVertices * sizeof(float);

    CreateCommittedBuffer(
        _dataSize, 
        D3D12_HEAP_TYPE_DEFAULT, 
        GetDefaultResourceStateFromHeapType(D3D12_HEAP_TYPE_DEFAULT), 
        D3D12CommandQueueContext, 
        _buffer,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    createCpuWritableBuffer(_dataSize, D3D12CommandQueueContext, _uploadBuffer);

    {
        createCpuReadableBuffer(_dataSize, D3D12CommandQueueContext, _readbackBuffer);

        ID3D11DeviceContext *pD3D11Context = D3D12CommandQueueContext->GetDeviceContext();

        CComPtr<ID3D11Device> pDevice;
        pD3D11Context->GetDevice(&pDevice);

        D3D11_BUFFER_DESC desc;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE;
        desc.ByteWidth = _numElements * _numVertices * sizeof(float);
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = 0;
        desc.StructureByteStride = sizeof(float);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        ThrowFailure(pDevice->CreateBuffer(&desc, nullptr, &_d3d11Buffer));
    }

    _uav = AllocateUAV(D3D12CommandQueueContext, _buffer, DXGI_FORMAT_R32_FLOAT, _numElements * _numVertices);
    
    return true;
}

}  // end namespace Osd

}  // end namespace OPENSUBDIV_VERSION
}  // end namespace OpenSubdiv

