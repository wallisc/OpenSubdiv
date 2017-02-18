//
//   Copyright 2015 Pixar
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

#include "../osd/d3d12ComputeEvaluator.h"

#include <cassert>
#include <sstream>
#include <string>

#include <d3d12.h>
#include "d3dx12.h"

#include <d3dcompiler.h>


#include "../far/error.h"
#include "../far/stencilTable.h"

#include "d3d12util.h"

namespace OpenSubdiv {
namespace OPENSUBDIV_VERSION {

namespace Osd {

static const char *shaderSource =
#include "../osd/hlslComputeKernel.gen.h"
;

// ----------------------------------------------------------------------------

// must match constant buffer declaration in hlslComputeKernel.hlsl
__declspec(align(16))

struct KernelUniformArgs {

    int start;     // batch
    int end;

    int srcOffset;
    int dstOffset;
};

static OSD_D3D12_GPU_VIRTUAL_ADDRESS createSRV(ID3D12Resource *buffer,
                                           DXGI_FORMAT format,
                                           ID3D12Device *device,
                                           size_t size) {
    return (OSD_D3D12_GPU_VIRTUAL_ADDRESS)buffer->GetGPUVirtualAddress();
}

D3D12StencilTable::D3D12StencilTable(Far::StencilTable const *stencilTable,
                                     D3D12CommandQueueContext *D3D12CommandQueueContext)
 {
    ID3D12Device *pDevice = D3D12CommandQueueContext->GetDevice();

    _numStencils = stencilTable->GetNumStencils();
    if (_numStencils > 0) {
        std::vector<int> const &sizes = stencilTable->GetSizes();

        ScopedCommandListAllocatorPair pair(D3D12CommandQueueContext, D3D12CommandQueueContext->GetCommandListAllocatorPair());
        ID3D12GraphicsCommandList *pCommandList = pair._commandList;

        createBufferWithVectorInitialData(sizes, D3D12CommandQueueContext, pCommandList, _sizesBuffer);
        createBufferWithVectorInitialData(stencilTable->GetOffsets(), D3D12CommandQueueContext, pCommandList, _offsetsBuffer);
        createBufferWithVectorInitialData(stencilTable->GetControlIndices(), D3D12CommandQueueContext, pCommandList, _indicesBuffer);
        createBufferWithVectorInitialData(stencilTable->GetWeights(), D3D12CommandQueueContext, pCommandList, _weightsBuffer);

        ThrowFailure(pCommandList->Close());
        D3D12CommandQueueContext->ExecuteCommandList(pCommandList);

        _sizes   = createSRV(_sizesBuffer,   DXGI_FORMAT_R32_SINT, pDevice,
                             stencilTable->GetSizes().size());
        _offsets = createSRV(_offsetsBuffer, DXGI_FORMAT_R32_SINT, pDevice,
                             stencilTable->GetOffsets().size());
        _indices = createSRV(_indicesBuffer, DXGI_FORMAT_R32_SINT, pDevice,
                             stencilTable->GetControlIndices().size());
        _weights= createSRV(_weightsBuffer, DXGI_FORMAT_R32_FLOAT, pDevice,
                            stencilTable->GetWeights().size());
    } else {
        _sizes = _offsets = _indices = _weights = NULL;
    }
}

D3D12StencilTable::~D3D12StencilTable() {
}

// ---------------------------------------------------------------------------


D3D12ComputeEvaluator::D3D12ComputeEvaluator() :
    _workGroupSize(64) {

}

D3D12ComputeEvaluator *
D3D12ComputeEvaluator::Create(BufferDescriptor const &srcDesc,
    BufferDescriptor const &dstDesc,
    BufferDescriptor const &duDesc,
    BufferDescriptor const &dvDesc,
    D3D12CommandQueueContext *D3D12CommandQueueContext) {
    return Create(
        srcDesc,
        dstDesc,
        duDesc,
        dvDesc,
        BufferDescriptor(),
        BufferDescriptor(),
        BufferDescriptor(),
        D3D12CommandQueueContext);
}

D3D12ComputeEvaluator *
D3D12ComputeEvaluator::Create(BufferDescriptor const &srcDesc,
                              BufferDescriptor const &dstDesc,
                              BufferDescriptor const &duDesc,
                              BufferDescriptor const &dvDesc,
                              BufferDescriptor const &duuDesc,
                              BufferDescriptor const &duvDesc,
                              BufferDescriptor const &dvvDesc,
                              D3D12CommandQueueContext *D3D12CommandQueueContext) {

    // TODO: implements derivatives
    (void)duDesc;
    (void)dvDesc;

    D3D12ComputeEvaluator *instance = new D3D12ComputeEvaluator();
    if (instance->Compile(srcDesc, dstDesc, D3D12CommandQueueContext)) return instance;
    delete instance;
    return NULL;
}

D3D12ComputeEvaluator::~D3D12ComputeEvaluator() {
}

bool
D3D12ComputeEvaluator::Compile(BufferDescriptor const &srcDesc,
                               BufferDescriptor const &dstDesc,
                               D3D12CommandQueueContext *D3D12CommandQueueContext) {

    if (srcDesc.length > dstDesc.length) {
        Far::Error(Far::FAR_RUNTIME_ERROR,
                   "srcDesc length must be less than or equal to "
                   "dstDesc length.\n");
        return false;
    }


    ID3D12Device *device = D3D12CommandQueueContext->GetDevice();
    assert(device);

    {

        UINT parameterIndex = 0;
        CD3DX12_ROOT_PARAMETER rootParameters[7];
        rootParameters[SizeRootSRVSlot].InitAsShaderResourceView(1);
        rootParameters[OffsetRootSRVSlot].InitAsShaderResourceView(2);
        rootParameters[IndexRootSRVSlot].InitAsShaderResourceView(3);
        rootParameters[WeightRootSRVSlot].InitAsShaderResourceView(4);
        rootParameters[SourceUAVSlot].InitAsUnorderedAccessView(0);
        rootParameters[DestinationUAVSlot].InitAsUnorderedAccessView(1);
        rootParameters[KernelUniformArgsRootConstantSlot].InitAsConstants(sizeof(KernelUniformArgs) / sizeof(UINT32), 0);

        D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = CD3DX12_ROOT_SIGNATURE_DESC(ARRAYSIZE(rootParameters), rootParameters);

        CComPtr<ID3DBlob> rootSignatureBlob, errorBlob;
        ThrowFailure(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootSignatureBlob, &errorBlob));

        CComPtr<ID3D12RootSignature> rootSignatureComPtr;
        ThrowFailure(device->CreateRootSignature(D3D12CommandQueueContext->GetNodeMask(), rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignatureComPtr)));
        _rootSignature.AddRefAndAttach(D3D12CommandQueueContext, rootSignatureComPtr);
    }

    DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(D3D10_SHADER_RESOURCES_MAY_ALIAS)
     dwShaderFlags |= D3D10_SHADER_RESOURCES_MAY_ALIAS;
#endif

#ifdef _DEBUG
    dwShaderFlags |= D3DCOMPILE_DEBUG;
#endif

    bool UseStructuredBuffers = true;

    std::ostringstream ss;
    ss << srcDesc.length;  std::string lengthValue(ss.str()); ss.str("");
    ss << srcDesc.stride;  std::string srcStrideValue(ss.str()); ss.str("");
    ss << dstDesc.stride;  std::string dstStrideValue(ss.str()); ss.str("");
    ss << _workGroupSize;  std::string workgroupSizeValue(ss.str()); ss.str("");
    ss << UseStructuredBuffers;  std::string useStructuredBuffersValue(ss.str()); ss.str("");

    D3D_SHADER_MACRO defines[] =
        { "LENGTH", lengthValue.c_str(),
          "SRC_STRIDE", srcStrideValue.c_str(),
          "DST_STRIDE", dstStrideValue.c_str(),
          "WORK_GROUP_SIZE", workgroupSizeValue.c_str(),
          "USE_STRUCTURED_BUFFERS", useStructuredBuffersValue.c_str(),
          0, 0 };

    LPCSTR shaderEntrypointName[] = { "cs_singleBuffer", "cs_separateBuffer" };
    for (UINT i = 0; i < NumberOfCSTypes; i++)
    {
        CComPtr<ID3DBlob> shaderBlob;
        CComPtr<ID3DBlob> errorBuffer;

        HRESULT hr = D3DCompile(shaderSource, strlen(shaderSource),
            NULL, &defines[0], NULL,
            shaderEntrypointName[i], "cs_5_0",
            dwShaderFlags, 0,
            &shaderBlob, &errorBuffer);

        if (FAILED(hr)) {
            if (errorBuffer != NULL) {
                Far::Error(Far::FAR_RUNTIME_ERROR,
                    "Error compiling HLSL shader: %s\n",
                    (CHAR*)errorBuffer->GetBufferPointer());
                return false;
            }
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.CS = CD3DX12_SHADER_BYTECODE(shaderBlob);
        desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        desc.NodeMask = D3D12CommandQueueContext->GetNodeMask();
        desc.pRootSignature = _rootSignature;

        CComPtr<ID3D12PipelineState> computePSOComPtr;
        ThrowFailure(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&computePSOComPtr)));
        _computePSOs[i].AddRefAndAttach(D3D12CommandQueueContext, computePSOComPtr);
    }
    
    return true;
}

/* static */
void
D3D12ComputeEvaluator::Synchronize(D3D12CommandQueueContext *D3D12CommandQueueContext) {
    D3D12CommandQueueContext->Syncronize();
}

bool
D3D12ComputeEvaluator::EvalStencils(ID3D12Resource *srcUAV,
                                    BufferDescriptor const &srcDesc,
                                    ID3D12Resource *dstUAV,
                                    BufferDescriptor const &dstDesc,
                                    OSD_D3D12_GPU_VIRTUAL_ADDRESS sizesSRV,
                                    OSD_D3D12_GPU_VIRTUAL_ADDRESS offsetsSRV,
                                    OSD_D3D12_GPU_VIRTUAL_ADDRESS indicesSRV,
                                    OSD_D3D12_GPU_VIRTUAL_ADDRESS weightsSRV,
                                    int start,
                                    int end,
                                    D3D12CommandQueueContext *D3D12CommandQueueContext) const {
    assert(D3D12CommandQueueContext);

    int count = end - start;
    if (count <= 0) return true;

    KernelUniformArgs args;
    args.start = start;
    args.end = end;
    args.srcOffset = srcDesc.offset;
    args.dstOffset = dstDesc.offset;

    ScopedCommandListAllocatorPair pair(D3D12CommandQueueContext, D3D12CommandQueueContext->GetCommandListAllocatorPair());
    ID3D12GraphicsCommandList *pCommandList = pair._commandList;
    pCommandList->SetComputeRootSignature(_rootSignature.Get());

    // Bind constants
    pCommandList->SetComputeRoot32BitConstants(KernelUniformArgsRootConstantSlot, sizeof(KernelUniformArgs) / sizeof(UINT32), &args, 0);

    // Bind SRVs
    pCommandList->SetComputeRootShaderResourceView(SizeRootSRVSlot, sizesSRV);
    pCommandList->SetComputeRootShaderResourceView(OffsetRootSRVSlot, offsetsSRV);
    pCommandList->SetComputeRootShaderResourceView(IndexRootSRVSlot, indicesSRV);
    pCommandList->SetComputeRootShaderResourceView(WeightRootSRVSlot, weightsSRV);

    // Bind the source UAV
    pCommandList->SetComputeRootUnorderedAccessView(SourceUAVSlot, srcUAV->GetGPUVirtualAddress());
    const bool bOnlyUsingSourceUAV = srcUAV == dstUAV;
    if (bOnlyUsingSourceUAV) {
        pCommandList->SetPipelineState(_computePSOs[SingleBufferCSIndex].Get());
    } else {
        pCommandList->SetPipelineState(_computePSOs[SeparateBufferCSIndex].Get());
        pCommandList->SetComputeRootUnorderedAccessView(DestinationUAVSlot, dstUAV->GetGPUVirtualAddress());
    }

    pCommandList->Dispatch((count + _workGroupSize - 1) / _workGroupSize, 1, 1);
    
    ThrowFailure(pCommandList->Close());
    D3D12CommandQueueContext->ExecuteCommandList(pCommandList);

    return true;
}

}  // end namespace Osd

}  // end namespace OPENSUBDIV_VERSION
}  // end namespace OpenSubdiv
