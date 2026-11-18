#pragma once

#include <atomic>
#include <vector>

// What the screen needs to know about the sound, written by the audio thread
// and read by the display. Only simple atomic values and a ring of samples,
// so the audio never waits for the screen.
struct LiveInfo
{
    // The most recently played note, for the scope, filter curves and envelope dots.
    std::atomic<float> noteHz { 0.0f };
    std::atomic<bool>  noteActive { false };
    std::atomic<float> liveCutoffHz[3] { 0.0f, 0.0f, 0.0f };
    std::atomic<float> gateLevel[3] { 1.0f, 1.0f, 1.0f };
    std::atomic<int>   filterEnvStage { 0 }, ampEnvStage { 0 };
    std::atomic<float> filterEnvSeconds { 0.0f }, ampEnvSeconds { 0.0f };
    std::atomic<float> filterEnvLevel { 0.0f }, ampEnvLevel { 0.0f };
    std::atomic<float> lfoPhase { 0.0f }, lfo2Phase { 0.0f };
    std::atomic<bool>  limiting { false }; // the Output Limiter turned something down (the screen clears it)

    // Output meter. outPeak is the loudest sample since the screen last
    // took it (the screen resets it to 0); outRms is a ~300 ms average.
    // outClipped stays on until the screen clears it. 1.0 = 0 dB.
    std::atomic<float> outPeak { 0.0f }, outRms { 0.0f };
    std::atomic<bool>  outClipped { false };
    std::atomic<int>   activeVoices { 0 };

    // The sound at each stage of the chain, sample by sample:
    // 0 oscillator, 1-3 after each filter position, 4 after EQ, 5 output.
    static constexpr int numStages = 6;
    struct Frame { float v[numStages]; };
    static constexpr int ringSize = 1 << 15;

    std::vector<Frame> ring = std::vector<Frame> (ringSize);
    std::atomic<int> writePos { 0 };

    // Copies the most recent n frames (oldest first). Safe from the UI thread;
    // n must be well below ringSize.
    void copyLatest (Frame* dest, int n) const
    {
        const int end = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
            dest[i] = ring[(size_t) ((end - n + i) & (ringSize - 1))];
    }
};
