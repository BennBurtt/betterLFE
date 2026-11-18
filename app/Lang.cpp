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
    return found != table.end() ? juce::String::fromUTF8 (found->second) : english;
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
