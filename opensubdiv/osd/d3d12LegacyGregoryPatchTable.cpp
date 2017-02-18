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

#include <memory>

#include "../osd/D3D12LegacyGregoryPatchTable.h"
#include "d3d12commandqueuecontext.h"

#include <D3D12.h>
#include "D3Dx12.h"
#include "d3d12util.h"

namespace OpenSubdiv {
namespace OPENSUBDIV_VERSION {

namespace Osd {

D3D12LegacyGregoryPatchTable::D3D12LegacyGregoryPatchTable() :
    _vertexSRV(0), _vertexValenceSRV(0), _quadOffsetsSRV(0) {
    _quadOffsetsBase[0] = _quadOffsetsBase[1] = 0;
}

D3D12LegacyGregoryPatchTable::~D3D12LegacyGregoryPatchTable() {
}

D3D12LegacyGregoryPatchTable *
D3D12LegacyGregoryPatchTable::Create(Far::PatchTable const *farPatchTable,
                                     D3D12CommandQueueContext *D3D12CommandQueueContext) {
    ID3D12Device *pDevice = D3D12CommandQueueContext->GetDevice();

    std::unique_ptr<D3D12LegacyGregoryPatchTable> result = std::unique_ptr<D3D12LegacyGregoryPatchTable>(new D3D12LegacyGregoryPatchTable());

    Far::PatchTable::VertexValenceTable const &
        valenceTable = farPatchTable->GetVertexValenceTable();
    Far::PatchTable::QuadOffsetsTable const &
        quadOffsetsTable = farPatchTable->GetQuadOffsetsTable();

    ScopedCommandListAllocatorPair commandListPair(D3D12CommandQueueContext, D3D12CommandQueueContext->GetCommandListAllocatorPair());
    ID3D12GraphicsCommandList *commandList = commandListPair._commandList;
    if (! valenceTable.empty()) {
        // bd.StructureByteStride = sizeof(unsigned int);
        createBufferWithVectorInitialData(valenceTable, D3D12CommandQueueContext, commandList, result->_vertexValenceBuffer);

        result->_vertexValenceSRV = result->_vertexValenceBuffer->GetGPUVirtualAddress();
    }

    if (! quadOffsetsTable.empty()) {
        //bd.StructureByteStride = sizeof(unsigned int);
        createBufferWithVectorInitialData(quadOffsetsTable, D3D12CommandQueueContext, commandList, result->_quadOffsetsBuffer);

        // srvd.Format = DXGI_FORMAT_R32_SINT;
        result->_quadOffsetsSRV = result->_quadOffsetsBuffer->GetGPUVirtualAddress();
    }

    // Execute the initial data upload
    {
        ThrowFailure(commandList->Close());

        D3D12CommandQueueContext->ExecuteCommandList(commandList);
    }

    result->_quadOffsetsBase[0] = 0;
    result->_quadOffsetsBase[1] = 0;

    // scan patchtable to find quadOffsetsBase.
    for (int i = 0; i < farPatchTable->GetNumPatchArrays(); ++i) {
        // GREGORY_BOUNDARY's quadoffsets come after GREGORY's.
        if (farPatchTable->GetPatchArrayDescriptor(i) ==
            Far::PatchDescriptor::GREGORY) {
            result->_quadOffsetsBase[1] = farPatchTable->GetNumPatches(i) * 4;
            break;
        }
    }
    return result.release();
}

void
D3D12LegacyGregoryPatchTable::UpdateVertexBuffer(
    ID3D12Resource *vbo, int numVertices, int numVertexElements,
    D3D12CommandQueueContext *D3D12CommandQueueContext) {
    UNREFERENCED_PARAMETER(D3D12CommandQueueContext);
    UNREFERENCED_PARAMETER(numVertices);
    UNREFERENCED_PARAMETER(numVertexElements);
    
    _vertexSRV = vbo->GetGPUVirtualAddress();
}

}  // end namespace Osd

}  // end namespace OPENSUBDIV_VERSION
}  // end namespace OpenSubdiv
