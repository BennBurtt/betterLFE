// Test signals for SubGUARD: the 22 checks of the analysis (score, alerts,
// transients) and 14 for Room Sim Pro (one thing at a time). Shared by the
// test file generator (tools/TestFiles.cpp) and the app's built-in test
// source, so that a file and the built-in signal of the same name are the
// same sound. Made at 48 kHz, in the order L, R, C, LFE (surrounds silent).
// Portable C++17.

#pragma once

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace subguard
{

struct TestFrame { double l = 0, r = 0, c = 0, lfe = 0; };

struct TestScene
{
    std::string file, title, lookFor;       // English
    std::string titleFr, lookForFr;         // French
    double seconds = 20.0;                  // length of one pass; the app loops it
    bool roomSimPro = false;                // the Room Sim Pro set
    std::function<std::function<TestFrame (double)>()> make;     // a fresh generator: t in seconds (t < 0: silence)
};

struct TestSignals
{
    static constexpr int sampleRate = 48000;
    static const std::vector<TestScene>& all();
};

} // namespace subguard
