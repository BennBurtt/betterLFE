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
            { "Bass on each playback system against the mix as approved (cinema, sub on time, in polarity), 20-120 Hz", "Le grave sur chaque système de lecture face au mix validé (cinéma, sub à l'heure, en polarité), 20-120 Hz" },
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
