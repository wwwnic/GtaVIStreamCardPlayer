#pragma once

namespace gp {

// Raises this process's GPU scheduling class to High (D3DKMT, gdi32), so the
// presenting and generation work is scheduled ahead of normal apps sharing
// the GPU. Own process only; nothing to undo at exit. Logs the outcome.
// Call after a Direct3D device exists: before the first GPU context Windows
// has no scheduling record for the process.
void RaiseGpuPriority();

}  // namespace gp
