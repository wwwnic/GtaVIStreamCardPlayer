// The generator the Presenter asks for Generated Frames (ADR 0003): on the GPU
// side, after the frame was converted to RGB.
#pragma once

#include <cstdint>
#include <memory>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;

namespace gp {

// Makes the Generated Frames between Unique Frame N-1 and Unique Frame N.
// Every call comes from the Presenter's thread, which must never be held up:
// anything slow (starting, loading a runtime) is the implementation's own
// business, off that thread, answering 0 Generated Frames meanwhile.
class FrameGenerator {
public:
    // Unique Frame N as the Presenter holds it.
    struct Input {
        ID3D11Device* device = nullptr;          // the Presenter's own; another one after a device rebuild
        ID3D11DeviceContext* context = nullptr;  // its immediate context
        // BGRA, the Feed's size, render target and shader resource. The
        // Presenter draws the next Unique Frame over it: copy what must last.
        ID3D11Texture2D* picture = nullptr;
        ID3D11ShaderResourceView* pictureView = nullptr;
        uint32_t width = 0, height = 0;
        uint64_t sequence = 0;        // Frame::sequence
        // The Unique Frames given before must not be used (a start, a pause
        // over 150 ms, another picture size, a device rebuild): start again
        // from this one.
        bool forgetEarlier = false;
        // False: no Generated Frame would be shown with this Unique Frame
        // (core/frame_schedule.h says when). It is still given, as N-1 for
        // the next.
        bool mayGenerate = false;
    };

    static constexpr int kCannotBeUsed = -1;

    virtual ~FrameGenerator() = default;
    virtual void NotePresented() {}  // successful present while this generator is in use

    // Takes Unique Frame N and answers how many Generated Frames between N-1
    // and N are ready to show now: 0, 1 or 3. kCannotBeUsed: not at all (no
    // runtime, a failed call); the Presenter lets go of the generator and
    // shows Unique Frames alone. It may leave any Direct3D state changed.
    virtual int Generate(const Input& frame) = 0;

    // Generated Frame `index` (0 first; under what Generate answered) of the
    // latest Generate: the Feed's size, shown like the picture. Stays valid
    // until the next Generate or ReleaseDevice.
    virtual ID3D11ShaderResourceView* Generated(int index) = 0;

    // The device is going away (lost, or the Presenter stops), or the
    // Presenter lets go of this generator: release everything made on it.
    // Generate may be called again later, with another device.
    virtual void ReleaseDevice() = 0;
};

// The developer switch (--frames-per-unique 2 or 4): no generating at all,
// each Unique Frame is shown that many times with its picture unchanged. It
// stands in for a generator so the timing can be checked in the log.
std::shared_ptr<FrameGenerator> MakeRepeatingGenerator(int timesShown);

}  // namespace gp
