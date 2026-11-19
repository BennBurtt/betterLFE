// Test signals: see TestSignals.h.

#include "TestSignals.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <random>

namespace subguard
{

namespace
{
    using Frame = TestFrame;

    constexpr double pi = 3.14159265358979323846;
    constexpr int sampleRate = 48000;

    // LFE recorded 10 dB down plays as loud as a screen channel
    const double asHeard = std::pow (10.0, -10.0 / 20.0);

    //==========================================================================
    /** Low-frequency noise, about 15-180 Hz, the same sequence for the same seed. */
    class Rumble
    {
    public:
        explicit Rumble (uint32_t seed, double rmsLevel = 0.05) : state (seed * 2654435761u + 12345u), level (rmsLevel)
        {
            // measure the raw level once so that the output has the requested RMS
            double sum = 0.0;
            Rumble* self = this;

            for (int i = 0; i < 96000; ++i)
            {
                const double v = self->raw();
                sum += v * v;
            }

            scale = level / std::sqrt (sum / 96000.0);
            state = seed * 2654435761u + 12345u;
            for (auto& v : lp) v = 0.0;
            for (auto& v : hp) v = 0.0;
        }

        double next()   { return raw() * scale; }

    private:
        double raw()
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            double x = (double) (state & 0xffffff) / (double) 0x800000 - 1.0;

            const double kl = 1.0 - std::exp (-2.0 * pi * 180.0 / sampleRate);
            const double kh = 1.0 - std::exp (-2.0 * pi * 15.0 / sampleRate);

            for (auto& v : lp)  { v += kl * (x - v); x = v; }
            for (auto& v : hp)  { v += kh * (x - v); x -= v; }

            return x;
        }

        uint32_t state;
        double level, scale = 1.0;
        double lp[3] = {}, hp[2] = {};
    };

    // A kick: pitch sweep 120 -> 45 Hz, 2 ms attack
    double kick (double t)
    {
        if (t < 0.0)
            return 0.0;

        const double phase = 2.0 * pi * (45.0 * t + 75.0 * 0.03 * (1.0 - std::exp (-t / 0.03)));
        return std::sin (phase) * std::exp (-t / 0.09) * (1.0 - std::exp (-t / 0.002));
    }

    // A low boom: single frequency, slow attack
    double boom (double t, double hz)
    {
        if (t < 0.0)
            return 0.0;

        return std::sin (2.0 * pi * hz * t) * std::exp (-t / 0.25) * (1.0 - std::exp (-t / 0.03));
    }

    // Time since the last hit, for hits every `period` seconds
    double sinceHit (double t, double period)   { return t < 0.0 ? -1.0 : std::fmod (t, period); }

    //==========================================================================

    /** A second-order low-pass or high-pass (Butterworth, bilinear), for band-limited test signals. */
    struct TestFilter
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        static TestFilter make (double hz, bool high)
        {
            TestFilter f;
            const double k = std::tan (pi * hz / sampleRate), q = std::sqrt (0.5), norm = 1.0 / (1.0 + k / q + k * k);
            f.b0 = high ? norm : k * k * norm;
            f.b1 = high ? -2.0 * norm : 2.0 * f.b0;
            f.b2 = f.b0;
            f.a1 = 2.0 * (k * k - 1.0) * norm;
            f.a2 = (1.0 - k / q + k * k) * norm;
            return f;
        }

        double process (double x)
        {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };

    // French titles and what to look for, by file name
    const std::map<std::string, std::pair<std::string, std::string>>& frenchTexts()
    {
        static const std::map<std::string, std::pair<std::string, std::string>> texts {
            { "01_clean_mix", { "Mix propre : l'écran porte le grave, LFE sans rapport et 10 dB dessous", "Score proche de 100, aucune alerte." } },
            { "02_lfe_copies_centre", { "Le LFE est une copie du centre, aussi fort à l'écoute", "Score 0, enceinte C en alerte, heat-map ambre, courbe C à +1." } },
            { "03_lfe_inverted_copy", { "Le LFE est le centre en polarité inversée", "Score 0, heat-map rouge, courbe C à -1 ; alerte aussi en mode « Out of phase only »." } },
            { "04_lfe_copy_5ms_late", { "Le LFE est une copie du centre, 5 ms en retard", "Score bas ; la courbe C passe de +1 en bas à -1 vers 100 Hz." } },
            { "05_lfe_only", { "Tout le grave dans le LFE, écran muet", "Score 20 à 25, « LFE dependence » bas, aucune alerte de phase." } },
            { "06_lfe_6dB_above_screen", { "LFE sans rapport, 6 dB au-dessus de l'écran", "Score 20 à 30, ligne « LFE against the screen » en orange." } },
            { "07_lfe_equal_unrelated", { "LFE sans rapport, aussi fort que l'écran", "Score 60 à 70 : pas d'interaction, mais la moitié du grave repose sur le LFE." } },
            { "08_lfe_partly_shared", { "LFE 10 dB sous le centre, copié à 60 % sur lui", "Score vers 60 à 65 ; courbe C vers +0,6." } },
            { "09_per_channel", { "L en phase avec le LFE, R hors phase, C sans rapport", "Enceinte L en ambre, R en rouge, C au repos. Le score reste haut : ce que L ajoute, R le retire." } },
            { "10_overlap_60_120", { "Écran et LFE forts et sans rapport", "Marques de recouvrement ambre sous 60-90 et 90-120 Hz." } },
            { "11_transients_aligned", { "Kicks au centre, LFE au même instant", "Bandeau Transients : « Aligned, in phase »." } },
            { "12_transients_lfe_12ms_late", { "Kicks au centre, LFE 12 ms en retard", "Bandeau Transients : « LFE late 12.0 ms »." } },
            { "13_transients_lfe_6ms_early", { "Kicks au centre, LFE 6 ms en avance", "Bandeau Transients : « LFE early 6.0 ms »." } },
            { "14_transients_inverted", { "Kicks au centre, LFE au même instant, polarité inversée", "Bandeau Transients : « Aligned, polarity inverted »." } },
            { "15_transients_very_quiet", { "Comme le 12, 60 dB plus bas", "« LFE late 12.0 ms », comme à plein niveau. La Phase Alert s'allume comme au 12 (elle ne dépend pas du niveau), mais le score reste près de 100 : à ce niveau, ce grave ne s'entend pas (la raison s'affiche sous le score)." } },
            { "16_transients_low_boom_under_bed", { "Booms à 28 Hz au centre sous un fond 60-180 Hz plus fort ; boom du LFE 8 ms en retard", "« LFE late » vers 8 à 10 ms avec un ± large, ou « timing not measurable »." } },
            { "17_level_calibration", { "Sinusoïdes : 40 Hz à -20 dBFS dans le LFE, 80 Hz à -20 dBFS au centre, 8 Hz à -30 dBFS dans le LFE", "Au curseur : LFE -20,0 dB à 40 Hz, LCR -23,0 dB à 80 Hz, LFE -30,0 dB à 8 Hz." } },
            { "18_silence", { "Silence numérique", "Score « -- », rien ne bouge." } },
            { "19_music_kick_in_lfe_12ms_late", { "Musique : basse, kick et charleston ; le LFE porte la basse et le kick, 12 ms en retard", "Bandeau Transients : « LFE late » vers 12 ms à chaque kick." } },
            { "20_music_kick_in_lfe_inverted", { "Même musique ; kick du LFE à l'heure, polarité inversée", "« Aligned, polarity inverted »." } },
            { "21_music_kick_not_in_lfe", { "Même musique ; le LFE ne porte que la basse", "Les attaques du mix sont comptées, aucune n'est dans le LFE." } },
            { "22_unlike_lfe_send_200ms_late", { "Kick au-dessus de 60 Hz au centre ; le LFE répond par un coup à 30 Hz, 200 ms plus tard", "« LFE attack late » vers 200 ms." } },

            { "P01_bass_L_only", { "Du grave dans L seulement",
              "Lentille 1, LCR : les têtes près de L sont les plus fortes. LFE : gris foncé. Lentille 2 : seule la cause Salle s'allume ; Enceintes et Mix restent gris (une enceinte seule, pas de LFE)." } },
            { "P02_bass_C_only", { "Du grave dans C seulement", "Comme P01, autour de C." } },
            { "P03_bass_R_only", { "Du grave dans R seulement", "Comme P01, en miroir : autour de R." } },
            { "P04_bass_LR_same", { "Le même grave dans L et R (basse mono en centre fantôme)",
              "Lentille 2, Enceintes : bleu. Dans le mix validé, L et R s'additionnent exactement ; dans la salle, ils arrivent à des instants différents. Mix : gris. C'est le lieu, pas le mix." } },
            { "P05_bass_LR_unrelated", { "Deux graves différents dans L et R",
              "Lentille 2, Enceintes : presque gris, contrairement à P04 : deux sons sans rapport ne peuvent pas interférer." } },
            { "P06_lfe_only", { "Tout le grave dans le LFE, écran muet",
              "Lentille 1, LFE : là où portent les caissons ; LCR : rien. Lentille 2, Enceintes : les deux caissons l'un contre l'autre. Mix : gris." } },
            { "P07_C_and_lfe_unrelated", { "C et LFE aussi forts, sans rapport", "Lentille 2, Mix : tout gris. Un LFE qui ne partage rien avec C ne peut ni s'y ajouter ni l'annuler." } },
            { "P08_lfe_copy_of_C", { "Le LFE est une copie de C, 6 dB plus bas",
              "Lentille 2, Mix : bleu. Dans le mix validé, C et LFE s'additionnent exactement ; dans la salle, caissons et C arrivent à des instants différents. C'est ce que l'assistant mixage travaillera." } },
            { "P09_lfe_copy_of_C_inverted", { "La même copie, polarité inversée",
              "Lentille 2, Mix : rouge, l'inverse de P08. Dans le mix validé, le LFE inversé retire du grave à C ; dans la salle, il en retire moins." } },
            { "P10_lfe_copy_of_C_5ms_late", { "La copie de C, 5 ms en retard",
              "Dans le mix validé, 5 ms de retard annulent C vers 100 Hz. La salle défait en partie l'annulation : lentille 2, Mix rouge, et en lentille 1 des têtes plus fortes que le mix." } },
            { "P11_inaudible_bass", { "C et LFE 60 dB plus bas (vers -86 dBFS)",
              "Lentille 1 : toutes les têtes gris foncé, 0 phone, comme le mix. Lentille 2 : grise et plate, personne n'entend ce grave. Notes à 100." } },
            { "P12_very_low_only", { "Du grave dans C, de 20 à 60 Hz", "Vue en couches : la couche du milieu (très grave) domine." } },
            { "P13_low_only", { "Du grave dans C, de 60 à 120 Hz", "Vue en couches : seule la couche du haut (grave) bouge." } },
            { "P14_infra_only", { "Une sinusoïde à 12 Hz dans le LFE, rien d'autre",
              "Vue en couches : seule la couche du bas (infra) bouge, en dB au-dessus du seuil. Les têtes restent gris foncé : rien n'est entendu de 20 à 120 Hz." } },
        };
        return texts;
    }
}

const std::vector<TestScene>& TestSignals::all()
{
    static const std::vector<TestScene> scenes = []
    {
        std::vector<TestScene> out;

    // Shared building blocks. Each scene builds fresh generators so that every
        // file is the same from one run to the next.
        const auto bed = [] (uint32_t seed, double level = 0.05) { return std::make_shared<Rumble> (seed, level); };
    
        const auto add = [&out] (std::string file, std::string title, std::string lookFor, double seconds,
                                 std::function<std::function<Frame (double)>()> make)
        {
            out.push_back ({ std::move (file), std::move (title), std::move (lookFor), {}, {}, seconds, false, std::move (make) });
        };
    
        add ("01_clean_mix", "Clean mix: screen channels carry the bass, LFE unrelated and 10 dB below them",
             "Score near 100. No alert, speakers idle, curves near 0.", 20.0, [bed]
             {
                 auto l = bed (1), c = bed (2), r = bed (3), x = bed (4);
                 return [=] (double) { return Frame { l->next(), r->next(), c->next(), 0.316 * asHeard * x->next() }; };
             });
    
        add ("02_lfe_copies_centre", "LFE is a copy of the centre channel, as loud when heard",
             "Score 0. C speaker alerts, amber heat map, C curve at +1.", 20.0, [bed]
             {
                 auto l = bed (1), c = bed (2), r = bed (3);
                 return [=] (double) { const double v = c->next(); return Frame { 0.3 * l->next(), 0.3 * r->next(), v, asHeard * v }; };
             });
    
        add ("03_lfe_inverted_copy", "LFE is the centre channel with its polarity flipped",
             "Score 0. C speaker alerts, red heat map, C curve at -1. Still alerts in 'Out of phase only'. Session polarity for C goes negative.",
             20.0, [bed]
             {
                 auto l = bed (1), c = bed (2), r = bed (3);
                 return [=] (double) { const double v = c->next(); return Frame { 0.3 * l->next(), 0.3 * r->next(), v, -asHeard * v }; };
             });
    
        add ("04_lfe_copy_5ms_late", "LFE is a copy of the centre channel, 5 ms late",
             "Low score. C curve swings from +1 at the bottom to -1 at 100 Hz (5 ms is half a period there).", 20.0, [bed]
             {
                 auto l = bed (1), c = bed (2), r = bed (3);
                 auto line = std::make_shared<std::vector<double>> ((size_t) (0.005 * sampleRate), 0.0);
                 auto pos = std::make_shared<size_t> (0);
                 return [=] (double)
                 {
                     const double v = c->next(), late = (*line)[*pos];
                     (*line)[*pos] = v;
                     *pos = (*pos + 1) % line->size();
                     return Frame { 0.3 * l->next(), 0.3 * r->next(), v, asHeard * late };
                 };
             });
    
        add ("05_lfe_only", "All the bass in the LFE, screen channels silent",
             "Score about 20-25: the room alone decides the bass. 'LFE dependence' low, LFE carries 100 %. No phase alert.", 20.0, [bed]
             {
                 auto x = bed (4);
                 return [=] (double) { return Frame { 0, 0, 0, asHeard * x->next() }; };
             });
    
        add ("06_lfe_6dB_above_screen", "Unrelated LFE, 6 dB above the screen channels when heard",
             "Score about 20-30. LFE-against-screen line in orange (about +6 dB). No phase alert.", 20.0, [bed]
             {
                 auto c = bed (2), x = bed (4);
                 return [=] (double) { return Frame { 0, 0, c->next(), 2.0 * asHeard * x->next() }; };
             });
    
        add ("07_lfe_equal_unrelated", "Unrelated LFE, as loud as the screen channels when heard",
             "Score about 60-70: no interaction, but half the bass rests on the LFE.", 20.0, [bed]
             {
                 auto c = bed (2), x = bed (4);
                 return [=] (double) { return Frame { 0, 0, c->next(), asHeard * x->next() }; };
             });
    
        add ("08_lfe_partly_shared", "LFE 10 dB below the centre channel, 60 % copied from it",
             "Score about 60, 'dangerous but passable'. C curve around +0.6, C speaker alerts.", 20.0, [bed]
             {
                 auto c = bed (2), x = bed (4);
                 return [=] (double) { const double v = c->next(); return Frame { 0, 0, v, 0.316 * asHeard * (0.6 * v + 0.8 * x->next()) }; };
             });
    
        add ("09_per_channel", "L shares its bass with the LFE in phase, R out of phase, C is unrelated",
             "L speaker alerts in amber, R in red, C stays idle. L curve positive, R curve negative. The score stays high: what L adds to the LFE, R takes away, so the screen as a whole does not interact with it.", 20.0, [bed]
             {
                 auto shared = bed (5), opposed = bed (6), c = bed (2);
                 return [=] (double)
                 {
                     const double a = shared->next(), b = opposed->next();
                     return Frame { a, b, 0.5 * c->next(), 0.5 * asHeard * (a - b) };
                 };
             });
    
        add ("10_overlap_60_120", "Screen and LFE both loud and unrelated",
             "Overlap marks under 60-90 and 90-120 Hz turn amber. No phase alert.", 20.0, [bed]
             {
                 auto c = bed (2, 0.1), x = bed (4, 0.1);
                 return [=] (double) { return Frame { 0, 0, c->next(), asHeard * x->next() }; };
             });
    
        // Transients: a hit in the centre channel every 1.5 s, with the LFE variant named in the title
        const auto hits = [&add, bed] (std::string file, std::string title, std::string lookFor, double lfeDelay, double lfeGain,
                                       double level, bool lowBoom)
        {
            add (std::move (file), std::move (title), std::move (lookFor), 20.0, [=]
            {
                auto bedC = bed (2, lowBoom ? 0.04 : 0.0005), bedLfe = bed (4, 0.0005);
                auto high = std::make_shared<double> (0.0);
                return [=] (double t)
                {
                    const double s = sinceHit (t, 1.5), late = sinceHit (t - lfeDelay, 1.5);
                    const auto shape = [lowBoom] (double u) { return lowBoom ? boom (u, 28.0) : kick (u); };
                    double c = 0.4 * shape (s), lfe = 0.4 * asHeard * lfeGain * shape (late);
    
                    // for the low boom, the centre also carries a louder bed above 60 Hz
                    double noise = bedC->next();
    
                    if (lowBoom)
                    {
                        *high += 0.008 * (noise - *high);
                        noise = 4.0 * (noise - *high);
                    }
    
                    return Frame { 0, 0, level * (c + noise), level * (lfe + bedLfe->next()) };
                };
            });
        };
    
        hits ("11_transients_aligned", "Kicks in the centre channel, the LFE hits at the same instant",
              "Transients strip: 'Aligned, in phase'.", 0.0, 1.0, 1.0, false);
        hits ("12_transients_lfe_12ms_late", "Kicks in the centre channel, the LFE hits 12 ms late",
              "Transients strip: 'LFE late 12.0 ms'.", 0.012, 1.0, 1.0, false);
        hits ("13_transients_lfe_6ms_early", "Kicks in the centre channel, the LFE hits 6 ms early",
              "Transients strip: 'LFE early 6.0 ms'.", -0.006, 1.0, 1.0, false);
        hits ("14_transients_inverted", "Kicks in the centre channel, the LFE hits at the same instant with flipped polarity",
              "Transients strip: 'Aligned, polarity inverted'.", 0.0, -1.0, 1.0, false);
        hits ("15_transients_very_quiet", "Same as file 12, 60 dB lower (peaks around -68 dBFS)",
              "Transients strip: 'LFE late 12.0 ms', exactly as at full level. The Phase Alert lights as for file 12 (it does not depend on the level), "
              "but the score stays near 100: at this level the bass is not heard (the reason shows under the score). Set the level range to -120 dB to see the curves.",
              0.012, 1.0, 0.001, false);
        hits ("16_transients_low_boom_under_bed", "28 Hz booms in the centre under a louder 60-180 Hz bed; the LFE boom is 8 ms late",
              "Transients strip: 'LFE late' around 8-10 ms with a wide +/-, or 'timing not measurable': a single low frequency is hard to time.",
              0.008, 1.0, 1.0, true);
    
        add ("17_level_calibration", "Pure tones: 40 Hz at -20 dBFS in the LFE, 80 Hz at -20 dBFS in the centre, 8 Hz at -30 dBFS in the LFE",
             "Cursor read-out: LFE -20.0 dB at 40 Hz, LCR -23.0 dB at 80 Hz (the screen sum is shown 3 dB down), LFE -30.0 dB at 8 Hz.",
             20.0, []
             {
                 return [] (double t)
                 {
                     if (t < 0.0)
                         return Frame {};
    
                     return Frame { 0, 0, 0.1 * std::sin (2.0 * pi * 80.0 * t),
                                    0.1 * std::sin (2.0 * pi * 40.0 * t) + 0.0316 * std::sin (2.0 * pi * 8.0 * t) };
                 };
             });
    
        add ("18_silence", "Digital silence", "Score shows '--'. Nothing moves.", 10.0, [] { return [] (double) { return Frame {}; }; });
    
        // Music-like: bass line in L, R and LFE, kick (thump and click) in the centre, hi-hat off the beat in L and R
        const auto music = [&add] (std::string file, std::string title, std::string lookFor, bool lfeKick, double lfeDelay, double lfeGain)
        {
            add (std::move (file), std::move (title), std::move (lookFor), 30.0, [=]
            {
                auto rng = std::make_shared<std::mt19937> (77u);
                auto low = std::make_shared<double> (0.0);
                return [=] (double t)
                {
                    if (t < 0.0)
                        return Frame {};
    
                    std::normal_distribution<double> normal (0.0, 1.0);
                    const double beat = 0.5, onBeat = std::fmod (t, beat), offBeat = std::fmod (t + beat / 2.0, beat);
                    const double click = onBeat < 0.004 ? normal (*rng) * std::exp (-onBeat / 0.0012) : 0.0;
                    const double white = normal (*rng);
                    *low += 0.35 * (white - *low);
                    const double hat = offBeat < 0.05 ? (white - *low) * std::exp (-offBeat / 0.012) : 0.0;
                    const double bass = 0.25 * std::sin (2.0 * pi * 55.0 * t) * (0.7 + 0.3 * std::sin (2.0 * pi * 0.5 * t))
                                          + 0.1 * std::sin (2.0 * pi * 110.0 * t);
                    const double lfe = 0.3 * asHeard * std::sin (2.0 * pi * 55.0 * t + 1.0)
                                         + (lfeKick ? 0.5 * asHeard * lfeGain * kick (std::fmod (t - lfeDelay + beat, beat)) : 0.0);
                    return Frame { 0.5 * (0.7 * bass + 0.15 * hat), 0.5 * (0.7 * bass - 0.12 * hat), 0.5 * (0.5 * kick (onBeat) + 0.25 * click), 0.5 * lfe };
                };
            });
        };
    
        music ("19_music_kick_in_lfe_12ms_late", "Bass line, kick and hi-hat; the LFE carries the bass and the kick, 12 ms late",
               "Transients strip: 'LFE late' around 12 ms on every kick. With Release lowered to about 10 %, the hi-hats are found too: 'nothing in the LFE'.",
               true, 0.012, 1.0);
        music ("20_music_kick_in_lfe_inverted", "Same music; the LFE kick is on time with flipped polarity",
               "Transients strip: the kicks are in the LFE: 'Aligned, polarity inverted'.", true, 0.0, -1.0);
        music ("21_music_kick_not_in_lfe", "Same music; the LFE only carries the bass line, no kick",
               "Transients strip: attacks of the mix are counted, none of them in the LFE.", false, 0.0, 1.0);
    
        add ("22_unlike_lfe_send_200ms_late", "Kick above 60 Hz in the centre; the LFE answers with a 30 Hz thud, 200 ms late",
             "Transients strip: 'LFE attack late' around 200 ms, timed attack against attack. With the search range under 200 ms: 'nothing in the LFE'.",
             20.0, []
             {
                 return [] (double t)
                 {
                     const double u = sinceHit (t, 1.5), v = sinceHit (t - 0.2, 1.5);
                     const double c = u < 0.0 ? 0.0 : 0.4 * std::sin (2.0 * pi * 95.0 * u) * std::exp (-u / 0.06) * (1.0 - std::exp (-u / 0.001));
                     const double lfe = v < 0.0 ? 0.0 : 0.3 * std::sin (2.0 * pi * 30.0 * v) * std::exp (-v / 0.2) * (1.0 - std::exp (-v / 0.01));
                     return Frame { 0, 0, c, lfe };
                 };
             });
    
        // Room Sim Pro: one thing at a time
        const auto addPro = [&out] (std::string file, std::string title, std::string lookFor, std::function<std::function<Frame (double)>()> make)
        {
            out.push_back ({ std::move (file), std::move (title), std::move (lookFor), {}, {}, 20.0, true, std::move (make) });
        };
    
        // A delay line, for an LFE that copies the centre late
        const auto delayed = [] (double seconds)
        {
            auto line = std::make_shared<std::vector<double>> ((size_t) std::max (1.0, seconds * sampleRate), 0.0);
            auto pos = std::make_shared<size_t> (0);
            return [line, pos] (double v)
            {
                const double out = (*line)[*pos];
                (*line)[*pos] = v;
                *pos = (*pos + 1) % line->size();
                return out;
            };
        };
    
        addPro ("P01_bass_L_only", "Bass in L only (15-180 Hz noise), nothing else",
                "Lens 1, L C R: the heads near the L speaker are the loudest. Lens 1, LFE: nothing (dark grey). "
                "Lens 2, Mix: all grey (no LFE, so no LFE / L C R interference). Lens 2, Speakers: grey (one speaker alone).",
                [bed] { auto l = bed (1); return [=] (double) { return Frame { l->next(), 0, 0, 0 }; }; });
        addPro ("P02_bass_C_only", "Bass in C only", "As P01, the C speaker the loudest place. Compare with P01 and P03: the bass follows its speaker.",
                [bed] { auto c = bed (2); return [=] (double) { return Frame { 0, 0, c->next(), 0 }; }; });
        addPro ("P03_bass_R_only", "Bass in R only", "As P01, mirrored: the heads near R get the most.",
                [bed] { auto r = bed (3); return [=] (double) { return Frame { 0, r->next(), 0, 0 }; }; });
        addPro ("P04_bass_LR_same", "The same bass in L and R (a mono bass as a phantom centre)",
                "Lens 2, Speakers: in the approved mix L and R add exactly in step; in the room they arrive at different times, "
                "so they add less: the heads go blue. Mix: grey (no LFE). This is the venue, not the mix.",
                [bed] { auto m = bed (5); return [=] (double) { const double v = m->next(); return Frame { 0.7 * v, 0.7 * v, 0, 0 }; }; });
        addPro ("P05_bass_LR_unrelated", "Different bass in L and in R (two unrelated noises)",
                "Lens 2, Speakers: grey, unlike P04: two unrelated signals cannot interfere. Lens 1 shows each side louder near its speaker.",
                [bed] { auto l = bed (1), r = bed (3); return [=] (double) { return Frame { 0.7 * l->next(), 0.7 * r->next(), 0, 0 }; }; });
        addPro ("P06_lfe_only", "All the bass in the LFE, the screen channels silent",
                "Lens 1, LFE: where the subwoofers reach. Lens 1, L C R: nothing. Lens 2, Speakers: the two subwoofers against each other. Mix: grey.",
                [bed] { auto x = bed (4); return [=] (double) { return Frame { 0, 0, 0, asHeard * x->next() }; }; });
        addPro ("P07_C_and_lfe_unrelated", "C and LFE as loud, unrelated to each other",
                "Lens 2, Mix: all grey: an LFE that shares nothing with C cannot add to it or cancel it. Lens 1: both parts lit.",
                [bed] { auto c = bed (2), x = bed (4); return [=] (double) { return Frame { 0, 0, c->next(), asHeard * x->next() }; }; });
        addPro ("P08_lfe_copy_of_C", "The LFE is a copy of C, 6 dB below it when heard",
                "Lens 2, Mix: in the approved mix C and the LFE add exactly in step; in the room the subwoofers and C arrive at "
                "different times, so they add less: the heads go blue (a loss caused by the mix). This is what the mixing assistant will work on.",
                [bed] { auto c = bed (2); return [=] (double) { const double v = c->next(); return Frame { 0, 0, v, 0.5 * asHeard * v }; }; });
        addPro ("P09_lfe_copy_of_C_inverted", "The LFE is a copy of C with its polarity flipped, 6 dB below it",
                "Lens 2, Mix: in the approved mix the inverted LFE takes bass away from C; in the room it takes away less: "
                "the heads go red (the room undoes part of the cancellation). The opposite of P08.",
                [bed] { auto c = bed (2); return [=] (double) { const double v = c->next(); return Frame { 0, 0, v, -0.5 * asHeard * v }; }; });
        addPro ("P10_lfe_copy_of_C_5ms_late", "The LFE is a copy of C, 5 ms late",
                "In the approved mix, 5 ms late cancels C around 100 Hz (half a period). In the room the cancellation is partly undone: "
                "lens 2, Mix goes red, and lens 1 shows heads louder than the mix. The layered view: the low layer moves more than the very low one.",
                [bed, delayed] { auto c = bed (2); auto late = delayed (0.005);
                                 return [=] (double) { const double v = c->next(); return Frame { 0, 0, v, asHeard * late (v) }; }; });
        addPro ("P11_inaudible_bass", "Bass in C and an unrelated LFE, 60 dB down (about -86 dBFS)",
                "Lens 1: every head dark grey, 0 phon, as the mix itself. Lens 2: grey and flat: nobody hears this bass, so nothing is shown, "
                "and the scores stay at 100.",
                [bed] { auto c = bed (2, 0.00005), x = bed (4, 0.00005); return [=] (double) { return Frame { 0, 0, c->next(), asHeard * x->next() }; }; });
        addPro ("P12_very_low_only", "Bass in C between 20 and 60 Hz only",
                "Layered view: only the middle layer (very low, 20-60 Hz) moves; the low layer says 'nothing in the mix'.",
                [bed] { auto c = bed (2, 0.25); auto f = std::make_shared<std::array<TestFilter, 4>>();
                        for (auto& x : *f) x = TestFilter::make (45.0, false);
                        return [=] (double) { double v = c->next(); for (auto& x : *f) v = x.process (v); return Frame { 0, 0, v, 0 }; }; });
        addPro ("P13_low_only", "Bass in C between 60 and 120 Hz only",
                "Layered view: only the top layer (low, 60-120 Hz) moves.",
                [bed] { auto c = bed (2, 0.12); auto f = std::make_shared<std::array<TestFilter, 5>>();
                        for (size_t i = 0; i < 4; ++i) (*f)[i] = TestFilter::make (75.0, true);
                        (*f)[4] = TestFilter::make (110.0, false);
                        return [=] (double) { double v = c->next(); for (auto& x : *f) v = x.process (v); return Frame { 0, 0, v, 0 }; }; });
        addPro ("P14_infra_only", "A 12 Hz tone in the LFE (-12 dBFS), nothing else",
                "Layered view: only the bottom layer (infra) moves; lens 1 gives it in dB above the threshold of feeling (not in phon).",
                [] { return [] (double t) { return Frame { 0, 0, 0, t < 0.0 ? 0.0 : 0.25 * std::sin (2.0 * pi * 12.0 * t) }; }; });
    
        // French titles and what to look for
        const auto& french = frenchTexts();

        for (auto& s : out)
        {
            const auto it = french.find (s.file);

            if (it != french.end())
            {
                s.titleFr = it->second.first;
                s.lookForFr = it->second.second;
            }
        }

        return out;
    }();

    return scenes;
}

} // namespace subguard
