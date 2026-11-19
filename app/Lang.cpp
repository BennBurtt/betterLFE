#include "Lang.h"

#include <juce_data_structures/juce_data_structures.h>

#include <unordered_map>

namespace subguard
{

namespace
{
    Language language = Language::english;

    // English -> French. A text missing here stays in English.
    const std::unordered_map<std::string, const char*>& french()
    {
        static const std::unordered_map<std::string, const char*> table {
            // Room Sim Pro: sections and settings
            { "Room", "Salle" }, { "Name", "Nom" }, { "Start from", "Partir de" }, { "Choose a room...", "Choisir une salle..." },
            { "Cinemas", "Cinémas" }, { "Exhibitions", "Expositions" }, { "Fashion shows", "Défilés de mode" }, { "Shops", "Boutiques" },
            { "Other venues", "Autres lieux" },
            { "Shape", "Forme" }, { "Rectangle", "Rectangle" }, { "Round", "Ronde" }, { "Oval", "Ovale" },
            { "Rounded corners", "Coins arrondis" }, { "L shape", "En L" },
            { "Diameter", "Diamètre" }, { "Length", "Longueur" }, { "Width", "Largeur" }, { "Height", "Hauteur" },
            { "Modes of this floor computed numerically (Rayleigh-Ritz): a second or two for a large room.",
              "Modes de ce sol calculés numériquement (Rayleigh-Ritz) : une à deux secondes pour une grande salle." },
            { "Acoustics", "Acoustique" }, { "Decay", "Décroissance" }, { "RT60 set by hand", "TR60 réglé à la main" },
            { "From the materials", "D'après les matériaux" }, { "RT60", "TR60" }, { "RT60 (Eyring)", "TR60 (Eyring)" },
            { "Front wall", "Mur avant" }, { "Back wall", "Mur arrière" }, { "Left wall", "Mur gauche" }, { "Right wall", "Mur droit" },
            { "Floor", "Sol" }, { "Ceiling", "Plafond" }, { "Wall", "Paroi" },
            { "Absorption in brackets: 63 Hz / 125 Hz", "Absorption entre parenthèses : 63 Hz / 125 Hz" },
            { "Schroeder frequency", "Fréquence de Schroeder" }, { "below it, single modes.", "en dessous, des modes isolés." },
            { "Sets", "Jeux" }, { "One set", "Un jeu" }, { "Two sets, back to back", "Deux jeux, dos à dos" },
            { "The second set is the first one turned round the centre of the room:\nplace the first set, the second follows.",
              "Le second jeu est le premier tourné autour du centre de la salle :\nplacez le premier, le second suit." },
            { "Left / Right", "Gauche / Droite" }, { "Left", "Gauche" }, { "Right", "Droite" }, { "Centre", "Centre" },
            { "Link", "Lien" }, { "On", "On" }, { "Polarity inverted", "Polarité inversée" }, { "Speaker on", "Enceinte en marche" },
            { "Linked: the right one mirrors the left one across the centre of the room.",
              "Liées : celle de droite est le miroir de celle de gauche par rapport au centre de la salle." },
            { "Linked: fills 2 and 4 mirror fills 1 and 3 across the centre of the room.",
              "Liés : les rappels 2 et 4 sont les miroirs des rappels 1 et 3 par rapport au centre de la salle." },
            { "From front", "Dist. avant" }, { "From left", "Dist. gauche" }, { "Delay", "Délai" }, { "Level", "Niveau" },
            { "Plays", "Joue" }, { "Lc / Rc (L or R + C at -3 dB)", "Lc / Rc (L ou R + C à -3 dB)" },
            { "Double-click: back to the default.", "Double-clic : retour à la valeur par défaut." },
            { "Divergence", "Divergence" }, { "Centre %", "Centre %" }, { "Front", "Avant" }, { "Rear", "Arrière" }, { "F / R", "Av / Ar" },
            { "Share of C played by the centre speaker; the rest goes to L and R as a phantom centre. Double-click: 100.",
              "Part de C jouée par l'enceinte centrale ; le reste part dans L et R en centre fantôme. Double-clic : 100." },
            { "100: each screen channel on its own speaker. Lower: spread over L, C and R. Double-click: 100.",
              "100 : chaque canal d'écran sur sa propre enceinte. Plus bas : réparti sur L, C et R. Double-clic : 100." },
            { "100: left fills play Lc, right fills Rc. Lower: the two sides share their feeds. Double-click: 100.",
              "100 : les rappels gauches jouent Lc, les droits Rc. Plus bas : les deux côtés partagent leurs signaux. Double-clic : 100." },
            { "100: the screen channels stay on the screen speakers. Lower: part of them goes to the fills. Double-click: 100.",
              "100 : les canaux d'écran restent sur les enceintes d'écran. Plus bas : une part va aux rappels. Double-clic : 100." },
            { "Subwoofers", "Caissons" }, { "Number", "Nombre" }, { "Sub", "Caisson" },
            { "Fill speakers", "Rappels" }, { "Use fill speakers", "Utiliser des rappels" }, { "1 pair", "1 paire" }, { "2 pairs", "2 paires" },
            { "Fills", "Rappels" }, { "Fill", "Rappel" },
            { "Audience", "Public" }, { "Layout", "Disposition" }, { "Rows", "Rangs" }, { "Two blocks, aisle in the middle", "Deux blocs, allée au milieu" },
            { "Rings around the centre", "Anneaux autour du centre" }, { "People", "Personnes" }, { "None (empty room)", "Aucune (salle vide)" },
            { "Seated", "Assises" }, { "Standing", "Debout" }, { "Inner", "Intérieur" }, { "Outer", "Extérieur" }, { "Rings", "Anneaux" },
            { "Outer ring", "Anneau ext." }, { "First row", "1er rang" }, { "Last row", "Dern. rang" }, { "Aisle", "Allée" },
            { "Per row", "Par rang" }, { "Ear", "Oreille" }, { "Rake", "Gradins" },
            { "Calibration", "Étalonnage" }, { "Levels set at", "Niveaux réglés" }, { "All seats (average)", "Toutes les places (moyenne)" },
            { "Reference seat (2/3, centre)", "Place de référence (2/3, centre)" },

            // Materials and their families
            { "Masonry and stone", "Maçonnerie et pierre" }, { "Boards and panels", "Plaques et panneaux" }, { "Glass", "Verre" },
            { "Floor coverings", "Revêtements de sol" }, { "Fabrics", "Tissus" }, { "Acoustic treatment", "Traitement acoustique" },
            { "Light structures", "Structures légères" }, { "Openings", "Ouvertures" },
            { "Brick, unglazed", "Brique non vernie" }, { "Concrete, bare or painted", "Béton brut ou peint" },
            { "Concrete block, coarse", "Parpaing brut" }, { "Concrete block, painted", "Parpaing peint" },
            { "Marble, stone or glazed tile", "Marbre, pierre ou carrelage" }, { "Plaster on brick or concrete", "Enduit sur brique ou béton" },
            { "Plasterboard on studs", "Plaque de plâtre sur ossature" }, { "Wood panelling on battens", "Lambris bois sur tasseaux" },
            { "Glass, large heavy panes", "Vitrage épais, grandes baies" }, { "Glass, ordinary window", "Vitrage ordinaire" },
            { "Carpet on concrete", "Moquette sur béton" }, { "Carpet on foam underlay", "Moquette sur sous-couche mousse" },
            { "Linoleum, vinyl or rubber on concrete", "Lino, vinyle ou caoutchouc sur béton" }, { "Wood floor on joists", "Parquet sur solives" },
            { "Heavy velour curtain, draped", "Rideau de velours lourd, drapé" },
            { "Bass traps (tuned membrane absorbers)", "Pièges à basses (membranes accordées)" }, { "Mineral wool 100 mm", "Laine minérale 100 mm" },
            { "Suspended acoustic ceiling tile", "Dalles de plafond acoustique suspendues" },
            { "Steel sheet cladding or roof", "Bardage ou toit en tôle d'acier" }, { "Tent fabric (PVC membrane)", "Toile de tente (membrane PVC)" },
            { "Open side (no wall)", "Côté ouvert (pas de mur)" },

            // Venues
            { "Small gallery", "Petite galerie" }, { "Art gallery, white cube", "Galerie d'art, white cube" },
            { "Museum hall, stone", "Salle de musée en pierre" }, { "Exhibition hall", "Hall d'exposition" },
            { "Convention centre hall", "Hall de centre des congrès" }, { "Oval rotunda", "Rotonde ovale" }, { "L-shaped gallery", "Galerie en L" },
            { "Showroom catwalk", "Défilé en showroom" }, { "Industrial hall, fashion show", "Halle industrielle, défilé" },
            { "Hangar catwalk", "Défilé en hangar" }, { "Tent catwalk", "Défilé sous tente" }, { "Palace ballroom", "Salle de bal de palace" },
            { "Small boutique", "Petite boutique" }, { "Fashion store", "Magasin de mode" }, { "Flagship store", "Flagship store" },
            { "Rounded-corner concept store", "Concept store à coins arrondis" }, { "Shopping mall atrium", "Atrium de centre commercial" },
            { "Tent, marquee", "Tente, chapiteau" }, { "Stone church", "Église en pierre" }, { "Living room", "Salon" },
            { "Studio control room, treated", "Régie de studio traitée" }, { "In the round, two L C R back to back", "En rond, deux L C R dos à dos" },

            // The rest of the interface
            { "Room Sim", "Room Sim" },
            { "Room Sim Pro", "Room Sim Pro" },
            { "Source & Format", "Source et format" },
            { "Spectrum", "Spectre" },
            { "Phase Alert", "Alerte de phase" },
            { "Transients", "Transitoires" },
            { "Playback chains", "Chaînes de lecture" },
            { "Dev: algorithm settings", "Dev : réglages des algorithmes" },
            { "Channel Format:", "Format des canaux :" },
            { "Input Device:", "Périphérique d'entrée :" },
            { "Refresh Rate:", "Rafraîchissement :" },
            { "Time / Frequency:", "Temps / fréquence :" },
            { "Level range:", "Plage de niveau :" },
            { "LFE +10 dB (theatre level)", "LFE +10 dB (niveau salle)" },
            { "Background animation", "Animation du fond" },
            { "Peak hold", "Maintien des crêtes" },
            { "Bass tilt (old display)", "Pente du grave (ancien affichage)" },
            { "Weigh by level balance", "Pondérer par l'équilibre des niveaux" },
            { "Inertia:", "Inertie :" },
            { "Integration:", "Intégration :" },
            { "Decay:", "Retombée :" },
            { "Transient check on", "Contrôle des transitoires actif" },
            { "Attack:", "Attaque :" },
            { "Hold:", "Maintien :" },
            { "Release:", "Relâchement :" },
            { "Sensitivity:", "Sensibilité :" },
            { "Search range (how far off the LFE may be):", "Plage de recherche (écart possible du LFE) :" },
            { "Likeness needed for a phase reading:", "Ressemblance requise pour lire la phase :" },
            { "Display time:", "Durée d'affichage :" },
            { "Defaults", "Défauts" },
            { "Score speed:", "Vitesse du score :" },
            { "Tolerance, Soft:", "Tolérance, Soft :" },
            { "Tolerance, Medium:", "Tolérance, Medium :" },
            { "Tolerance, Strong:", "Tolérance, Strong :" },
            { "Weight, cinema sub late:", "Poids, sub de cinéma en retard :" },
            { "Weight, cinema sub early:", "Poids, sub de cinéma en avance :" },
            { "LFE dependence, ceiling:", "Dépendance au LFE, plafond :" },
            { "Infra-bass weight:", "Poids de l'infra-grave :" },
            { "Infra-bass up to:", "Infra-grave jusqu'à :" },
            { "Full weight from:", "Poids plein dès :" },
            { "LFE level dependence, weight in score:", "Dépendance au niveau du LFE, poids dans le score :" },
            { "LFE level dependence, red at a mean of:", "Dépendance au niveau du LFE, rouge à une moyenne de :" },
            { "Room score, weight of the worst seats:", "Score de salle, poids des pires places :" },
            { "Room score, worst seats (share):", "Score de salle, pires places (part) :" },
            { "Reset", "Reset" },
            { "Room Sim: the bass seat by seat in fifteen cinema rooms, from a live corridor room to a treated IMAX.", "Room Sim : le grave place par place dans quinze salles de cinéma, de la salle couloir vive à l'IMAX traitée." },
            { "Room Sim Pro: three rooms of your own, every speaker placed freely, with fill speakers. Its own score, not part of the global one.", "Room Sim Pro : trois salles à vous, chaque enceinte placée librement, avec des rappels. Son propre score, hors du score global." },
            { "Click: solo. Option-click: mute.", "Clic : solo. Option-clic : mute." },
            { "Audio input permission denied", "Accès à l'entrée audio refusé" },
            { "Demo signal (no audio device)", "Signal de démo (pas de périphérique audio)" },
            { "No input device", "Pas de périphérique d'entrée" },
            { "In phase and out of phase", "En phase et en opposition" },
            { "Out of phase only", "Opposition seulement" },
            { "Out of phase only: alert just where the LFE works against the screen channel.", "Opposition seulement : alerte là où le LFE travaille contre le canal d'écran." },
            { "Display tilt of the first version: -12 dB at 20 Hz. Off: levels are true dBFS.", "Pente d'affichage de la première version : -12 dB à 20 Hz. Décoché : niveaux en vrais dBFS." },
            { "Off: the alert follows the correlation alone, however quiet the LFE is.\nOn: an LFE much quieter (or louder) than the screen channel counts less.", "Décoché : l'alerte suit la seule corrélation, si faible que soit le LFE.\nCoché : un LFE bien plus faible (ou plus fort) que le canal d'écran compte moins." },
            { "On: the LFE counts 10 dB louder than a screen channel, as a cinema plays it. Off: as recorded.\nIt applies everywhere: spectrum, alerts, playback chains and score.", "Coché : le LFE compte 10 dB plus fort qu'un canal d'écran, comme en salle. Décoché : tel qu'enregistré.\nS'applique partout : spectre, alertes, chaînes de lecture et score." },
            { "Soft", "Soft" },
            { "Medium", "Medium" },
            { "Strong", "Strong" },
            { "Soft: phase alert from a correlation of 0.90; the score tolerates 3 dB of change before counting it.", "Soft : alerte de phase dès une corrélation de 0,90 ; le score tolère 3 dB d'écart avant de compter." },
            { "Medium: phase alert from 0.75; the score tolerates 2 dB.", "Medium : alerte de phase dès 0,75 ; le score tolère 2 dB." },
            { "Strong: phase alert from 0.50; the score counts changes from 1 dB, about the smallest audible one.", "Strong : alerte de phase dès 0,50 ; le score compte les écarts dès 1 dB, à peu près le plus petit audible." },
            { "0 (fast)", "0 (rapide)" },
            { "0: the score follows the mix as it does now. Towards 1: more and more inertia, up to about ten seconds.", "0 : le score suit le mix comme maintenant. Vers 1 : de plus en plus d'inertie, jusqu'à une dizaine de secondes." },
            { "Off by default. Solo in your DAW what you want to check (a kick and its LFE send, for instance), then switch this on.", "Désactivé par défaut. Mettez en solo dans votre DAW ce que vous voulez vérifier (un kick et son envoi LFE, par exemple), puis activez." },
            { "How fast a rise must be to count as a transient: the time over which the peak is watched. Low: only sharp hits. High: slower swells count too.", "Vitesse de montée pour compter comme transitoire : la durée sur laquelle la crête est observée. Bas : seulement les coups secs. Haut : les montées plus lentes comptent aussi." },
            { "After a transient, no new one for this long: a kick, its click and its cymbal stay one hit.", "Après un transitoire, pas de nouveau pendant cette durée : un kick, son clic et sa cymbale restent un seul coup." },
            { "How long the tail of a hit hides what follows. Higher: a ringing tail is not taken for new transients. Lower: quieter hits soon after a loud one are found.", "Combien de temps la queue d'un coup masque la suite. Plus haut : une queue qui sonne n'est pas prise pour de nouveaux transitoires. Plus bas : des coups plus faibles juste après un fort sont trouvés." },
            { "How small a rise counts as a transient. Higher: more transients found, and more false ones.", "Plus petite montée comptée comme transitoire. Plus haut : plus de transitoires trouvés, et plus de faux." },
            { "The LFE is looked for this far before and after each attack of the mix. Wider finds a very late LFE; narrower is more precise on dense music and on weak hits.", "Le LFE est cherché jusqu'à cette distance avant et après chaque attaque du mix. Plus large : trouve un LFE très en retard ; plus étroit : plus précis sur une musique dense et des coups faibles." },
            { "Above this likeness between mix and LFE, the delay and polarity come from the correlation (precise). Below, the two attacks are timed against each other (coarse, no phase).", "Au-dessus de cette ressemblance entre mix et LFE, le délai et la polarité viennent de la corrélation (précis). En dessous, les deux attaques sont chronométrées l'une contre l'autre (grossier, sans phase)." },
            { "How long a verdict stays on screen.", "Durée d'affichage d'un verdict." },
            { "Clears the history strip and restarts the score.", "Efface la frise et relance le score." },
            { "Listening level: the sound pressure a screen channel gives with the calibration signal at -20 dBFS RMS (85 dB(C) in a cinema). It sets what is audible and how loud the bass is heard, which the score follows.", "Niveau d'écoute : la pression acoustique d'un canal d'écran avec le signal d'étalonnage à -20 dBFS RMS (85 dB(C) en salle). Il fixe ce qui est audible et la sonie du grave, que le score suit." },
            { "Rooms in the score (modal simulation, see the Room Sim tab):", "Salles dans le score (simulation modale, voir l'onglet Room Sim) :" },
            { "How many dB a band may change on a playback system before it starts to count, at each level.", "De combien de dB une bande peut changer sur un système de lecture avant de compter, à chaque niveau." },
            { "How much of the score a bass entirely in the LFE can take away. At 1.00 'LFE alone' scores near 2; about 0.80 puts it near 20.", "Ce qu'un grave entièrement dans le LFE peut retirer au score. À 1,00, « LFE seul » vaut environ 2 ; vers 0,80, environ 20." },
            { "What bass under the 'infra-bass up to' frequency still counts for in the LFE dependence; it rises to 1 at 'full weight from'.", "Ce que compte encore le grave sous la fréquence « infra-grave jusqu'à » dans la dépendance au LFE ; il monte à 1 à « poids plein dès »." },
            { "The most the LFE level dependence can take off the score.", "Le maximum que la dépendance au niveau du LFE peut retirer au score." },
            { "Mean difference from the expected bass at which the LFE level dependence box is fully red.", "Écart moyen au grave attendu auquel la case de dépendance au niveau du LFE est entièrement rouge." },
            { "Room Sim and Room Sim Pro: room score = (1 - weight) x mean seat score + weight x score of the worst seats. 0 is the plain mean.", "Room Sim et Room Sim Pro : score de salle = (1 - poids) x moyenne des places + poids x score des pires places. 0 donne la moyenne simple." },
            { "Which seats count as the worst ones: this share of the seats, at least one.", "Quelles places comptent comme les pires : cette part des places, au moins une." },
            { "Source", "Source" },
            { "Dev", "Dev" },
            { "Error: ", "Erreur : " },
            { "Needs ", "Il faut " },
            { " channels, device gives ", " canaux, le périphérique en donne " },
            { "No input device open", "Aucun périphérique d'entrée ouvert" },
            { "Listening ", "Écoute " },
            { "Listening level: a screen channel at -20 dBFS RMS gives", "Niveau d'écoute : un canal d'écran à -20 dBFS RMS donne" },
            { "   cinema calibration", "   étalonnage cinéma" },
            { "Bass playing now, per band:  ", "Grave joué maintenant, par bande :  " },
            { " dB SPL, ", " dB SPL, " },
            { "under 20 phon", "sous 20 phones" },
            { " phon", " phones" },
            { " dB double it", " dB le doublent" },
            { "Bass now:  ", "Grave :  " },
            { "SubGUARD Analyzer : 5Hz - 150Hz", "Analyseur SubGUARD : 5 Hz - 150 Hz" },
            { "Level (dB, tilted)", "Niveau (dB, pente)" },
            { "Level (dBFS)", "Niveau (dBFS)" },
            { "ROBUSTNESS", "ROBUSTESSE" },
            { "LCR sum", "Somme LCR" },
            { "Transients\nmix vs LFE", "Transitoires\nmix vs LFE" },
            { "early -", "avance -" },
            { "late +", "retard +" },
            { "Transient check off. Solo in your DAW what you want to check, then switch it on in the Transients panel.", "Contrôle des transitoires désactivé. Mettez en solo dans votre DAW ce que vous voulez vérifier, puis activez-le dans le panneau Transitoires." },
            { "waiting for an attack in the mix", "en attente d'une attaque dans le mix" },
            { "Attack in the mix: nothing in the LFE within ", "Attaque dans le mix : rien dans le LFE à moins de " },
            { "Widen the search range in the Transients panel if the LFE may be further off.", "Élargissez la plage de recherche dans le panneau Transitoires si le LFE peut être plus loin." },
            { "LFE late ", "LFE en retard " },
            { "LFE early ", "LFE en avance " },
            { "Aligned, polarity inverted", "Aligné, polarité inversée" },
            { "Aligned, in phase", "Aligné, en phase" },
            { "Aligned, phase shifted ", "Aligné, phase décalée " },
            { "likeness ", "ressemblance " },
            { "   sum ", "   somme " },
            { "LFE attack late ", "Attaque LFE en retard " },
            { "LFE attack early ", "Attaque LFE en avance " },
            { "LFE attack on time (within ", "Attaque LFE à l'heure (à moins de " },
            { "timed attack against attack: the LFE does not carry the same sound as the mix (likeness ", "chronométré attaque contre attaque : le LFE ne porte pas le même son que le mix (ressemblance " },
            { "), no phase reading", "), pas de lecture de phase" },
            { "In the LFE, timing not measurable", "Dans le LFE, timing non mesurable" },
            { "-1 out", "-1 opp." },
            { "+1 in", "+1 phase" },
            { "Frequency (Hz)", "Fréquence (Hz)" },
            { "Bass on each playback system against the mix as approved (cinema, sub on time, in polarity), 20-120 Hz. In each box: the mean, then the band that moves most down and most up",
              "Le grave sur chaque système de lecture face au mix validé (cinéma, sub à l'heure, en polarité), 20-120 Hz. Dans chaque case : la moyenne, puis la bande qui baisse le plus et celle qui monte le plus" },
            { "Stereo PA", "Façade stéréo" },
            { "5.1 PA + LFE", "Façade 5.1 + LFE" },
            { "Small speakers", "Petites enceintes" },
            { "Other system (not cinema)", "Other system (pas cinéma)" },
            { "Other: ", "Other : " },
            { "Other system: ", "Other system : " },
            { "none", "aucune" },
            { " dB at ", " dB à " },
            { "Screen channels in opposition of phase, summed: ", "Voies écran en opposition de phase, une fois sommées : " },
            { "Counted in the score: an other system is ticked in Room Sim. Not seen in cinema, where each channel has its own speaker.",
              "Compté dans le score : un other system est coché dans Room Sim. Invisible au cinéma, où chaque voie a son enceinte." },
            { "Not counted in the score: tick an other system in Room Sim. Not seen in cinema, where each channel has its own speaker.",
              "Pas compté dans le score : cocher un other system dans Room Sim. Invisible au cinéma, où chaque voie a son enceinte." },
            { "Below, in purple, the other systems: not cinema, electrical models of venues; their four bands in place of seats, against the same channels with nothing cancelled.",
              "En dessous, en violet, les other systems : pas du cinéma, des modèles électriques de lieux ; leurs quatre bandes à la place des places, face aux mêmes voies sans rien d'annulé." },
            { "OTHER SYSTEMS - not cinema: venues, electrical model, screen channels summed", "OTHER SYSTEMS - pas cinéma : lieux, modèle électrique, voies écran sommées" },
            { "Not cinema: it also counts the screen channels in opposition of phase.", "Pas cinéma : compte aussi les voies écran en opposition de phase." },
            { "Other system: stereo PA, subwoofers on L + R, no LFE", "Other system : façade stéréo, subs sur L + R, sans LFE" },
            { "Other system: 5.1 on a PA, subwoofers on L + R + LFE", "Other system : 5.1 sur une façade, subs sur L + R + LFE" },
            { "Other system: small speakers, no subwoofer", "Other system : petites enceintes, sans sub" },
            { "A stereo fold-down (L + 0.71 C, R + 0.71 C, the LFE left out) on a PA whose subwoofers play L + R, as at a show or in a shop",
              "Un repli stéréo (L + 0,71 C, R + 0,71 C, sans le LFE) sur une façade dont les subs jouent L + R, comme dans un défilé ou une boutique" },
            { "The 5.1 on a PA whose processor feeds the subwoofers L + R + C and the LFE (+10 dB), as at an event",
              "Le 5.1 sur une façade dont le processeur envoie aux subs L + R + C et le LFE (+10 dB), comme dans un événement" },
            { "A stereo fold-down on small speakers that roll off under 60 Hz (12 dB per octave), no subwoofer, no LFE",
              "Un repli stéréo sur de petites enceintes qui tombent sous 60 Hz (12 dB par octave), sans sub, sans LFE" },
            { "Cinema, sub in polarity", "Cinéma, sub en polarité" },
            { "Cinema, sub inverted", "Cinéma, sub inversé" },
            { "Stereo, no LFE", "Stéréo, sans LFE" },
            { "Bass managed", "Bass management" },
            { "+ encoder LFE filter", "+ filtre LFE d'encodeur" },
            { "     worst: room ", "     pire : salle " },
            { " (room score ", " (score de salle " },
            { "     worst system: ", "     pire système : " },
            { "Robustness", "Robustesse" },
            { " / 100     interaction x LFE dependence", " / 100     interaction x dépendance au LFE" },
            { "  from interaction", "  par l'interaction" },
            { "  from LFE dependence (audible)", "  par la dépendance au LFE (audible)" },
            { " / 100     LFE ", " / 100     LFE " },
            { " dB against the screen, carries ", " dB face à l'écran, porte " },
            { " % of the bass", " % du grave" },
            { "LFE level dependence", "Dépendance au niveau du LFE" },
            { "mean ", "moyenne " },
            { " dB  (max ", " dB  (max " },
            { "over all systems and subwoofer levels: the bass moves no more than it should", "sur tous les systèmes et niveaux de sub : le grave ne bouge pas plus qu'il ne doit" },
            { "over all systems and subwoofer levels: cancellation risk when the subwoofer is off its level", "sur tous les systèmes et niveaux de sub : risque d'annulation quand le sub n'est pas à son niveau" },
            { "over all systems and subwoofer levels: boost risk when the subwoofer is off its level", "sur tous les systèmes et niveaux de sub : risque de surplus quand le sub n'est pas à son niveau" },
            { "Electrical models: delay, polarity, filters and level, plus the rooms ticked below (modal simulation, Room Sim tab). Real rooms are not boxes: a score of 100 does not protect against a given room's own faults.", "Modèles électriques : délai, polarité, filtres et niveau, plus les salles cochées dessous (simulation modale, onglet Room Sim). Les vraies salles ne sont pas des boîtes : un score de 100 ne protège pas des défauts propres d'une salle donnée." },
            { "Bass robustness", "Robustesse du grave" },
            { "Fifteen rooms, twenty seats each (rectangular rooms, modal sum). Each dot is a seat: what the LFE / screen interaction does to its bass, against the mix as approved. The room's own colour is left out: a decorrelated LFE puts every dot on 0. Ticked rooms count in the score (click a row). Result = bass robustness x room score.", "Quinze salles, vingt places chacune (salles rectangulaires, somme modale). Chaque point est une place : ce que l'interaction LFE / écran fait à son grave, face au mix validé. La couleur propre de la salle est retirée : un LFE décorrélé met chaque point sur 0. Les salles cochées comptent dans le score (cliquer une ligne). Résultat = robustesse du grave x score de la salle." },
            { "Result", "Résultat" },
            { "seats: bass change from the LFE / screen interaction", "places : écart de grave dû à l'interaction LFE / écran" },
            { "Waiting for bass", "En attente de grave" },
            { "Alert", "Alerte" },
            { "LFE-dependent", "Dépend du LFE" },
            { "now", "maint." },
            { "Seat score", "Note des places" },
            { "Seat ", "Place " },
            { "Drag to turn, wheel to zoom, double-click to reset. Point at a seat for its score.  White: L C R   Orange: subwoofers   Blue: fills", "Glisser pour tourner, molette pour zoomer, double-clic pour revenir. Survoler une place pour sa note.  Blanc : L C R   Orange : caissons   Bleu : rappels" },
            { "Counts", "Compte" },
            { "This room counts in the Room Sim Pro score.", "Cette salle compte dans le score Room Sim Pro." },
            { "Save the three rooms to a file.", "Enregistrer les trois salles dans un fichier." },
            { "Load three rooms from a file.", "Charger trois salles depuis un fichier." },
            { "Room Sim Pro score", "Score Room Sim Pro" },
            { "Three rooms of your own, outside the cinema layout. Same rating as Room Sim and the same Dev settings and listening level. Ticked rooms count in this score, which is not part of the global one. Electrical robustness ", "Trois salles à vous, hors de l'implantation cinéma. Même note que Room Sim, mêmes réglages Dev et même niveau d'écoute. Les salles cochées comptent dans ce score, qui ne fait pas partie du score global. Robustesse électrique " },
            { "Round, ", "Ronde, " },
            { " m across, ", " m de diamètre, " },
            { " m high", " m de haut" },
            { " seats", " places" },
            { ",  fills", ",  rappels" },
            { "computing...", "calcul..." },
            { "room ", "salle " },
            { "Save the Room Sim Pro rooms", "Enregistrer les salles Room Sim Pro" },
            { "Load Room Sim Pro rooms", "Charger des salles Room Sim Pro" },
            { "Save config...", "Enregistrer..." },
            { "Load config...", "Charger..." },
            { "Fast (0.5 s · 4 Hz)", "Rapide (0,5 s · 4 Hz)" },
            { "Normal (1 s · 2 Hz)", "Normal (1 s · 2 Hz)" },
            { "Ultra (2 s · 1 Hz)", "Ultra (2 s · 1 Hz)" },
            { "Corridor", "Couloir" },
            { "Small multiplex", "Petit multiplexe" },
            { "Old cinema", "Ancien cinéma" },
            { "Narrow 120", "Étroite 120" },
            { "Screening room", "Salle de projection" },
            { "Multiplex 150", "Multiplexe 150" },
            { "Multiplex 200", "Multiplexe 200" },
            { "Multiplex 300", "Multiplexe 300" },
            { "Large 400", "Grande 400" },
            { "Mix room", "Salle de mixage" },
            { "Dubbing stage", "Auditorium" },
            { "Premium large", "Grand premium" },
            { "Large 400 treated", "Grande 400 traitée" },
            { "IMAX treated", "IMAX traitée" },

            { "same sound, confirmed", "même son, confirmé" }, { "similar sound", "son semblable" },
            { "close in time only, maybe another sound (likeness ", "seulement proche dans le temps, peut-être un autre son (ressemblance " },
            { "): timing attack against attack, no phase reading", ") : chronométré attaque contre attaque, pas de lecture de phase" },

            { "worst ", "pire " }, { "worst point ", "pire point " },

            { "As with pink noise and a sound level meter: each speaker group is set for the same total energy from 20 to 120 Hz (direct and reverberant sound together, which below the Schroeder frequency are the same modes). A live room therefore gets lower gains than a dead one.",
              "Comme au bruit rose et au sonomètre : chaque groupe d'enceintes est réglé pour la même énergie totale de 20 à 120 Hz (son direct et réverbéré ensemble, qui sous la fréquence de Schroeder sont les mêmes modes). Une salle vive reçoit donc des gains plus bas qu'une salle mate." },

            { "Soft:", "Soft :" }, { "Medium:", "Medium :" }, { "Strong:", "Strong :" },
            { "Weight when late:", "Poids en retard :" }, { "Weight when early:", "Poids en avance :" }, { "Ceiling:", "Plafond :" },
            { "Weight:", "Poids :" }, { "Up to:", "Jusqu'à :" }, { "Level dependence, weight:", "Dépendance au niveau, poids :" },
            { "Level dependence, red at:", "Dépendance au niveau, rouge à :" }, { "Worst seats, weight:", "Pires places, poids :" },
            { "Worst seats, share:", "Pires places, part :" },
            { "Tolerances (Soft, Medium, Strong)", "Tolérances (Soft, Medium, Strong)" }, { "Cinema subwoofer off its place", "Sub de cinéma mal placé" },
            { "LFE dependence", "Dépendance au LFE" }, { "Infra-bass", "Infra-grave" }, { "Rooms (Room Sim and Room Sim Pro)", "Salles (Room Sim et Room Sim Pro)" },
            { "Bass playing now, at the listening level", "Grave joué maintenant, au niveau d'écoute" }, { "Band", "Bande" }, { "Level", "Niveau" },
            { "Loudness", "Sonie" }, { "dB to double it", "dB pour la doubler" },

            { "hears ", "entend " }, { "feels ", "ressent " }, { " dB SPL under 40 Hz", " dB SPL sous 40 Hz" },
            { "Infra, under 20 Hz (not heard, felt)", "Infra, sous 20 Hz (ressenti plus qu'entendu)" }, { "Very low, 20-60 Hz", "Très grave, 20-60 Hz" },
            { "Low, 60-120 Hz", "Grave, 60-120 Hz" }, { "nothing in the mix", "rien dans le mix" }, { "very low", "très grave" }, { "low", "grave" },
            { "dip down, boost up:  0 dB", "creux en bas, boost en haut :  0 dB" },
            { "Bass layers against the approved mix. Drag to turn, as the 3D view.", "Couches du grave face au mix validé. Glisser pour tourner, comme la vue 3D." },

            { "bass ", "grave " }, { " dB against the approved mix", " dB face au mix validé" }, { "Coverage (Pro), weight:", "Couverture (Pro), poids :" },
            { "Room Sim Pro: how much the bass level at each seat against the approved mix counts (back rows losing the bass, holes, boosts). 0: only the interaction, as Room Sim.",
              "Room Sim Pro : ce que compte le niveau du grave à chaque place face au mix validé (rangs du fond qui perdent le grave, trous, boosts). 0 : seulement l'interaction, comme Room Sim." },

            { "How far apart the three layers are.", "Écart entre les trois calques." }, { "Layer spacing", "Écart des calques" },
            { "Bass layers against the approved mix. Drag to turn, wheel to zoom, double-click to reset.",
              "Couches du grave face au mix validé. Glisser pour tourner, molette pour zoomer, double-clic pour revenir." },

            { "How far a boost lifts a layer and a dip lowers it.", "De combien un boost soulève un calque et un creux l'abaisse." },
            { "Amplitude", "Amplitude" }, { "Settings", "Réglages" }, { "Fold the settings away", "Replier les réglages" },
            { "Show the room and speaker settings", "Afficher les réglages de la salle et des enceintes" },

            { "dip -9 dB", "creux -9 dB" }, { "+9 dB boost", "boost +9 dB" },

            // The two lenses of Room Sim Pro
            { "Screen", "Écran" },
            { "A screen drawn on the front wall, for a cinema. Only a mark: it changes nothing in the computation.",
              "Un écran dessiné sur le mur avant, pour un cinéma. Simple repère : il ne change rien au calcul." },
            { "Where the bass goes", "Répartition du grave" }, { "LFE / L C R interference", "Interférences LFE / LCR" },
            { "For the venue: the bass level at every point against the approved mix, the room and the placement of every speaker "
              "included. Blue: the bass is lost; red: too much. For placing the subwoofers, the fills and L C R.",
              "Pour le lieu : le niveau du grave en chaque point face au mix validé, salle et placement de chaque enceinte compris. "
              "Bleu : le grave se perd ; rouge : il y en a trop. Pour placer les caissons, les rappels et les LCR." },
            { "For the mix: what the LFE adds to or takes from L C R at every point, the room's modes included, against what it "
              "did in the approved mix. A speaker placed or set badly is not in it; an LFE that shares nothing with L C R shows nothing.",
              "Pour le mix : ce que le LFE ajoute aux LCR ou leur retire en chaque point, modes de la salle compris, face à ce qu'il "
              "faisait dans le mix validé. Une enceinte mal placée ou mal réglée n'y entre pas ; un LFE qui ne partage rien avec les LCR ne montre rien." },
            { "All", "Tout" }, { "All the bass heard.", "Tout le grave entendu." },
            { "L C R alone, through every speaker that plays them (Lc / Rc fills included): a hole here comes from L C R or the fills.",
              "Les LCR seuls, par toutes les enceintes qui les jouent (rappels Lc / Rc compris) : un trou ici vient des LCR ou des rappels." },
            { "The LFE alone, through the subwoofers and the LFE fills: a hole here comes from them.",
              "Le LFE seul, par les caissons et les rappels LFE : un trou ici vient d'eux." },
            { "Heads: the bass they get against the approved mix", "Têtes : le grave reçu face au mix validé" },
            { "Heads: the interference against the approved mix", "Têtes : l'interférence face au mix validé" },
            { "Coverage", "Répartition" }, { "Interference", "Interférences" }, { "interference ", "interférence " },
            { "Who gets bass, how much", "Qui a du grave, et combien" }, { "Why", "Pourquoi" },
            // Dips against peaks, ringing
            { "Dips against peaks, allowance:", "Creux face aux pics, marge :" },
            { "Room Sim and Room Sim Pro: a dip is heard less than a peak of the same size. 1: dips get the published "
              "difference (Bharitkar, Robinson & Poulain 2016; Toole & Olive), about 2.6 dB more below 63 Hz, "
              "2.2 dB at 125 Hz; 0: dips count as peaks.",
              "Room Sim et Room Sim Pro : un creux s'entend moins qu'un pic de même taille. 1 : les creux reçoivent l'écart publié "
              "(Bharitkar, Robinson et Poulain 2016 ; Toole et Olive), environ 2,6 dB de plus sous 63 Hz, 2,2 dB à 125 Hz ; "
              "0 : un creux compte comme un pic." },
            { "Ringing", "Résonance" },
            { "How long the bass rings at each seat: the decay of the room modes heard there, against the thresholds of "
              "Fazenda, Stephenson & Goldberg (2015). Grey: not heard; yellow: acceptable for music; orange to red: heard on music. "
              "Absorption in the bass shortens it. Not in the scores.",
              "Combien de temps le grave résonne à chaque place : la décroissance des modes qu'on y entend, face aux seuils de "
              "Fazenda, Stephenson et Goldberg (2015). Gris : inaudible ; jaune : acceptable pour la musique ; orange à rouge : "
              "audible sur de la musique. De l'absorption dans le grave la raccourcit. N'entre pas dans les notes." },
            { "not heard", "inaudible" }, { "music threshold", "seuil musique" }, { "x2", "x2" },
            { "heard on music", "audible sur de la musique" }, { "acceptable for music", "acceptable pour la musique" },
            { "Rings ", "Résonne " }, { " s at ", " s à " }, { " Hz (music threshold ", " Hz (seuil musique " },
            { "Heads: how long the bass rings, against the music threshold", "Têtes : durée de résonance du grave, face au seuil musique" },
            { "How long the bass rings, against the threshold heard on music (Fazenda et al. 2015)",
              "Durée de résonance du grave, face au seuil audible sur de la musique (Fazenda et coll. 2015)" },
            { "Decay of the modes heard there, worst band of the layer", "Décroissance des modes entendus, pire bande de la couche" },
            { "heard ringing: ", "résonance audible : " },
            // Test tab
            { "Test", "Test" }, { "Test signals", "Signaux de test" }, { "SubGUARD test signals", "Signaux de test SubGUARD" },
            { "The built-in test signals: choose \"SubGUARD test signals\" as the input device, then click a signal.",
              "Les signaux de test intégrés : choisir « Signaux de test SubGUARD » comme périphérique d'entrée, puis cliquer un signal." },
            { "Use the test signals as input", "Utiliser les signaux de test comme entrée" },
            { "Same as choosing \"SubGUARD test signals\" as the input device in Source.",
              "Comme choisir « Signaux de test SubGUARD » comme périphérique d'entrée dans Source." },
            { "Stop", "Arrêter" }, { "Load the rooms of the measurements", "Charger les salles des mesures" },
            { "Room Sim Pro: the three rooms the Room Sim Pro signals were measured in (small screening room, "
              "multiplex 200 seats, premium large format). Undo with Option-Z.",
              "Room Sim Pro : les trois salles où les signaux Room Sim Pro ont été mesurés (petite salle de projection, "
              "multiplexe 200 places, grand format premium). Annuler avec Option-Z." },
            { "Input: the test signals. Click a signal to play it; it loops. Level Strong, LFE +10 dB checked, as measured.",
              "Entrée : les signaux de test. Cliquer un signal pour le jouer, en boucle. Niveau Strong, LFE +10 dB coché, comme pour les mesures." },
            { "To hear them: Source > input device > \"SubGUARD test signals\" (or the button below).",
              "Pour les écouter : Source > périphérique d'entrée > « Signaux de test SubGUARD » (ou le bouton ci-dessous)." },
            { "The analysis: score, alerts, transients", "L'analyse : score, alertes, transitoires" },
            { "Room Sim Pro: one thing at a time (open the Room Sim Pro tab while it plays)",
              "Room Sim Pro : une chose à la fois (ouvrir l'onglet Room Sim Pro pendant la lecture)" },
            { "Built-in test signals, 48 kHz (Test tab)", "Signaux de test intégrés, 48 kHz (onglet Test)" },
            // Menus
            { "File", "Fichier" }, { "Edit", "Édition" }, { "Save the rooms...", "Enregistrer les salles…" },
            { "Load rooms...", "Charger des salles…" }, { "Undo", "Annuler" }, { "Redo", "Rétablir" },
            // Speaker models, aim, look-at point, hover
            { "Model", "Modèle" }, { "Aim", "Orientation" }, { "Tilt", "Inclinaison" },
            { "How the speaker radiates in the bass. Omnidirectional below about c / (pi x width): a screen speaker 76 cm wide "
              "up to about 140 Hz. Cardioid models send less bass behind them.",
              "Comment l'enceinte rayonne dans le grave. Omnidirectionnelle sous environ c / (pi x largeur) : une enceinte d'écran de "
              "76 cm de large jusque vers 140 Hz. Les modèles cardioïdes envoient moins de grave derrière eux." },
            { "Horizontal aim: 0 towards the back wall, positive towards the right wall. Double-click: 0.",
              "Orientation horizontale : 0 vers le mur du fond, positif vers le mur droit. Double-clic : 0." },
            { "Vertical aim: positive upwards. Double-click: 0.", "Inclinaison : positive vers le haut. Double-clic : 0." },
            { "Omnidirectional", "Omnidirectionnelle" },
            { "JBL 3730 ScreenArray (cinema screen)", "JBL 3730 ScreenArray (écran cinéma)" },
            { "EAW KF730 (line array element)", "EAW KF730 (élément de ligne source)" },
            { "Genelec 1238A (main monitor)", "Genelec 1238A (écoute principale)" },
            { "Cardioid (K&F PIA M cardioid mode, d&b CCL)", "Cardioïde (K&F PIA M en mode cardioïde, d&b CCL)" },
            { "Hypercardioid", "Hypercardioïde" },
            { "LFE fill speakers", "Rappels LFE" },
            { "Look at, from front", "Regard, depuis l'avant" }, { "Look at, from left", "Regard, depuis la gauche" },
            { "Where the audience looks (a screen, a catwalk, an object on show), marked by a cross. "
              "The placement assistant will put C and the phantom centre of L and R there.",
              "L'endroit que regarde le public (un écran, un podium, un objet exposé), marqué par une croix. "
              "L'assistant placement y mettra C et le centre fantôme de L et R." },
            { "looked at", "regard" },
            { "coverage score", "note de répartition" }, { "placement score", "note de placement" }, { "mix score", "note de mix" },
            { "Loudness of its bass: ", "Sonie de son grave : " }, { "L C R alone: ", "LCR seuls : " }, { "LFE alone: ", "LFE seul : " },
            { "Its bass level against the approved mix: ", "Son niveau de grave face au mix validé : " },
            { "Felt under 40 Hz: ", "Ressenti sous 40 Hz : " },
            { "How loud the bass is at every head and point (phon, ISO 226), on the same scale as the mix as approved, "
              "the room and the placement of every speaker included. A mix with little bass shows little everywhere.",
              "La sonie du grave à chaque tête et en chaque point (phones, ISO 226), sur la même échelle que le mix validé, "
              "salle et placement de chaque enceinte compris. Un mix avec peu de grave en montre peu partout." },
            { "Why the bass changes against the approved mix, cut in three causes that add up: the room (what each speaker "
              "gives there on its own), the speakers (their interference: placement, delay, polarity) and the mix (the LFE "
              "against L C R). Grey where the mix's bass is not heard.",
              "Pourquoi le grave change face au mix validé, en trois causes qui s'additionnent : la salle (ce que chaque enceinte "
              "donne seule à cet endroit), les enceintes (leurs interférences : placement, délai, polarité) et le mix (le LFE "
              "contre les LCR). Gris là où le grave du mix ne s'entend pas." },
            { "Speakers", "Enceintes" }, { "Mix", "Mix" }, { "Placement", "Placement" },
            { "What each speaker gives there on its own: modes, distance, materials. Placing the speakers or the audience changes it.",
              "Ce que chaque enceinte donne seule à cet endroit : modes, distance, matériaux. Le placement des enceintes ou du public le change." },
            { "The interference between speakers: subwoofer against subwoofer, fill against the speaker it repeats, L against R, "
              "two L C R sets. Placement, delay and polarity in the venue change it.",
              "Les interférences entre enceintes : caisson contre caisson, rappel contre l'enceinte qu'il répète, L contre R, "
              "deux jeux LCR. Le placement, le délai et la polarité dans le lieu le changent." },
            { "The interference between the LFE and L C R that the mix makes, against the approved mix. The LFE's delay, "
              "polarity, level and low-pass in the mix change it.",
              "L'interférence entre le LFE et les LCR que crée le mix, face au mix validé. Le délai, la polarité, le niveau et "
              "le filtre passe-bas du LFE dans le mix la changent." },
            { "the room", "la salle" }, { "the speakers", "les enceintes" }, { "the mix", "le mix" },
            { "Heads: how loud their bass is (phon)", "Têtes : la sonie de leur grave (phones)" },
            { "Heads: the change caused by the room, against the approved mix", "Têtes : l'écart dû à la salle, face au mix validé" },
            { "Heads: the change caused by the speakers, against the approved mix", "Têtes : l'écart dû aux enceintes, face au mix validé" },
            { "Heads: the change caused by the mix, against the approved mix", "Têtes : l'écart dû au mix, face au mix validé" },
            { "Loudness of the bass (phon; infra: dB above the threshold)", "Sonie du grave (phones ; infra : dB au-dessus du seuil)" },
            { "What changes the bass against the approved mix: ", "Ce qui change le grave face au mix validé : " },
            { " dB SL", " dB au-dessus du seuil" }, { "mix ", "mix " }, { " phon (mix ", " phones (mix " }, { "speakers ", "enceintes " },
            { "Neutral plane: the mix's own loudness", "Plan neutre : la sonie du mix lui-même" },
            { "Grey and flat where the mix's bass is not heard", "Gris et plat là où le grave du mix ne s'entend pas" },
            { "all of it", "tout" },
            { "Where the bass goes, against the approved mix", "Répartition du grave face au mix validé" },
            { "Interference between the LFE and L C R, against the approved mix", "Interférence entre le LFE et les LCR, face au mix validé" },
            { "Drag to turn, wheel to zoom, double-click to reset.", "Glisser pour tourner, molette pour zoomer, double-clic pour revenir." },
            { "Drag to turn, wheel to zoom, double-click to reset. Point at a head for its details.  White: L C R   Orange: subwoofers   Blue: fills",
              "Glisser pour tourner, molette pour zoomer, double-clic pour revenir. Pointer une tête pour son détail.  Blanc : LCR   Orange : caissons   Bleu : rappels" },

            // The fifteen cinema rooms
            { "Corridor room, live", "Salle couloir, vive" }, { "Small multiplex, live", "Petit multiplexe, vif" },
            { "Old cinema, near-cubic, live", "Ancien cinéma, presque cubique, vif" }, { "Narrow 120 seats", "Étroite 120 places" },
            { "Small screening room", "Petite salle de projection" }, { "Multiplex 150 seats", "Multiplexe 150 places" },
            { "Multiplex 200 seats", "Multiplexe 200 places" }, { "Multiplex 300 seats", "Multiplexe 300 places" },
            { "Large 400 seats", "Grande salle 400 places" }, { "Mix room (240 m3)", "Salle de mixage (240 m3)" },
            { "Dubbing stage", "Auditorium de mixage" }, { "Premium large format", "Grand format premium" },
            { "Large 400 seats, treated", "Grande salle 400 places, traitée" }, { "IMAX", "IMAX" }, { "IMAX, treated", "IMAX, traitée" },
        };

        return table;
    }

    juce::PropertiesFile::Options settingsOptions()
    {
        // The same file as the window's settings
        juce::PropertiesFile::Options storage;
        storage.applicationName = "SubGuard";
        storage.filenameSuffix = ".settings";
        storage.osxLibrarySubFolder = "Application Support";
        return storage;
    }
}

Language currentLanguage()          { return language; }
void setLanguage (Language l)       { language = l; }

juce::String tr (const juce::String& english)
{
    if (language == Language::english)
        return english;

    const auto& table = french();
    const auto found = table.find (english.toStdString());

    if (found != table.end())
        return juce::String::fromUTF8 (found->second);

    // Names built with numbers in them (the playback systems): translated piece by piece
    static const std::pair<const char*, const char*> pieces[] = {
        { "cinema, sub in place", "cinéma, sub en place" }, { "cinema, sub ", "cinéma, sub " }, { ", inverted", ", inversé" },
        { "bass managed ", "bass management " }, { ", encoder filter", ", filtre d'encodeur" },
        { "stereo downmix without LFE", "downmix stéréo sans LFE" },
    };
    auto text = english;

    for (const auto& [from, to] : pieces)
        text = text.replace (from, juce::String::fromUTF8 (to));

    return text;
}

Language savedLanguage()
{
    juce::PropertiesFile file (settingsOptions());
    return file.getValue ("language", "en") == "fr" ? Language::french : Language::english;
}

void saveLanguage (Language l)
{
    juce::PropertiesFile file (settingsOptions());
    file.setValue ("language", l == Language::french ? "fr" : "en");
    file.saveIfNeeded();
}

} // namespace subguard
