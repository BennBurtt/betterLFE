#pragma once

#include <array>
#include <cstring>
#include <initializer_list>
#include <utility>
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

    /** The four surround groups of the header: 0 wide, 1 side, 2 rear, 3 top (heights); -1 for L, C, R, LFE. */
    static int surroundGroupOf (const char* label)
    {
        const auto is = [label] (std::initializer_list<const char*> names)
        {
            for (const char* n : names)
                if (std::strcmp (label, n) == 0)
                    return true;

            return false;
        };

        if (is ({ "L", "C", "R", "LFE" }))                                                   return -1;
        if (is ({ "Lw", "Rw" }))                                                             return 0;
        if (is ({ "Ls", "Rs", "Lss", "Rss" }))                                               return 1;
        if (is ({ "Lb", "Rb", "Lr", "Rr", "Lsr", "Rsr", "Lrs", "Rrs", "Cs", "Bs" }))       return 2;
        return 3;       // Ltf, Rtf, Ltr, Rtr, Ltm, Rtm, Ts...: the heights
    }

    /** Positions of the surround and height channels, each with its group (surroundGroupOf). */
    std::vector<std::pair<int, int>> surroundGroups() const
    {
        std::vector<std::pair<int, int>> out;

        for (size_t i = 0; i < channels.size(); ++i)
            if (const int group = surroundGroupOf (channels[i]); group >= 0)
                out.push_back ({ (int) i, group });

        return out;
    }

    /** Whether the format has a channel in this surround group. */
    bool hasSurroundGroup (int group) const
    {
        for (const auto& c : surroundGroups())
            if (c.second == group)
                return true;

        return false;
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
