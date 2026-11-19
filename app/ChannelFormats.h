#pragma once

#include <array>
#include <cstring>
#include <vector>

namespace subguard
{

struct ChannelFormat
{
    const char* name;
    std::vector<const char*> channels;

    int indexOf (const char* label) const
    {
        for (size_t i = 0; i < channels.size(); ++i)
            if (std::strcmp (channels[i], label) == 0)
                return (int) i;

        return -1;
    }

    /** Positions of L, C, R, LFE in the input stream. */
    std::array<int, 4> analysisIndices() const
    {
        return { { indexOf ("L"), indexOf ("C"), indexOf ("R"), indexOf ("LFE") } };
    }

    /** Positions of every other channel (surrounds and heights): summed into S for the analysis. */
    std::vector<int> surroundIndices() const
    {
        std::vector<int> out;

        for (size_t i = 0; i < channels.size(); ++i)
            if (std::strcmp (channels[i], "L") != 0 && std::strcmp (channels[i], "C") != 0 && std::strcmp (channels[i], "R") != 0
                && std::strcmp (channels[i], "LFE") != 0)
                out.push_back ((int) i);

        return out;
    }
};

inline const std::vector<ChannelFormat>& channelFormats()
{
    static const std::vector<ChannelFormat> formats = {
        { "5.1 ITU - (L, R, C, LFE, Ls, Rs)",  { "L", "R", "C", "LFE", "Ls", "Rs" } },
        { "5.1 FILM - (L, C, R, Ls, Rs, LFE)", { "L", "C", "R", "Ls", "Rs", "LFE" } },
        { "5.1 DTS - (L, R, Ls, Rs, C, LFE)",  { "L", "R", "Ls", "Rs", "C", "LFE" } },
        { "7.1 DTS 1 - (L, C, R, Ls, Rs, Lr, Rr, LFE)", { "L", "C", "R", "Ls", "Rs", "Lr", "Rr", "LFE" } },
        { "7.1 DTS 2 - (L, R, C, LFE, Ls, Rs, Lr, Rr)", { "L", "R", "C", "LFE", "Ls", "Rs", "Lr", "Rr" } },
        { "5.1.2 - (L, R, C, LFE, Ls, Rs, Ltf, Rtf)",   { "L", "R", "C", "LFE", "Ls", "Rs", "Ltf", "Rtf" } },
        { "5.1.4 - (L, R, C, LFE, Ls, Rs, Ltf, Rtf, Ltr, Rtr)",
          { "L", "R", "C", "LFE", "Ls", "Rs", "Ltf", "Rtf", "Ltr", "Rtr" } },
        { "7.1.2 - (L, R, C, LFE, Ls, Rs, Lb, Rb, Ltf, Rtf)",
          { "L", "R", "C", "LFE", "Ls", "Rs", "Lb", "Rb", "Ltf", "Rtf" } },
        { "7.1.4 - (L, R, C, LFE, Ls, Rs, Lb, Rb, Ltf, Rtf, Ltr, Rtr)",
          { "L", "R", "C", "LFE", "Ls", "Rs", "Lb", "Rb", "Ltf", "Rtf", "Ltr", "Rtr" } },
        { "9.1.2 - (L, R, C, LFE, Lw, Rw, Ls, Rs, Lb, Rb, Ltf, Rtf)",
          { "L", "R", "C", "LFE", "Lw", "Rw", "Ls", "Rs", "Lb", "Rb", "Ltf", "Rtf" } },
        { "9.1.4 - (L, R, C, LFE, Lw, Rw, Ls, Rs, Lb, Rb, Ltf, Rtf, Ltr, Rtr)",
          { "L", "R", "C", "LFE", "Lw", "Rw", "Ls", "Rs", "Lb", "Rb", "Ltf", "Rtf", "Ltr", "Rtr" } },
    };

    return formats;
}

} // namespace subguard
