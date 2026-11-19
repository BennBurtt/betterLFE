# betterLFE — SubGUARD

Analyseur temps réel de l'interaction entre le LFE et les canaux d'écran
(L / C / R) entre 20 et 150 Hz, pour le mixage cinéma.
Application native C++ ([JUCE](https://juce.com) 8).

## Compiler sur macOS

Prérequis : outils en ligne de commande Xcode et CMake.

```bash
xcode-select --install
```

```bash
brew install cmake
```

```bash
./build_mac.sh
```

Le script télécharge JUCE, compile, lance les tests du moteur et indique le
chemin de `SubGuard.app`. Pour l'essayer sans carte son :

```bash
build/SubGuard_artefacts/Release/SubGuard.app/Contents/MacOS/SubGuard --demo
```

Le logo et l'icône sont dans `assets/`.

Le script crée aussi 22 fichiers de test 5.1 dans `test-files/`, avec ce que
l'app doit afficher pour chacun : voir [`docs/FICHIERS_TEST.md`](docs/FICHIERS_TEST.md).
Et 14 fichiers pour comprendre Room Sim Pro dans `test-files/room-sim-pro/`
(une seule chose par fichier, mesurée dans les trois salles par défaut) : voir
[`docs/FICHIERS_TEST_ROOM_SIM_PRO.md`](docs/FICHIERS_TEST_ROOM_SIM_PRO.md).

## Organisation

| Dossier | Contenu |
|---|---|
| `core/` | Moteur d'analyse en C++17 pur, sans dépendance (`LfeEngine`, `TransientDetector`, `PlaybackSim`, `AlertTracker`) |
| `app/` | Application JUCE : entrée audio, interface |
| `tests/` | Vérifications du moteur sur signaux synthétiques |
| `tools/` | Générateur des fichiers de test 5.1 |

## Ce qui est mesuré

Deux détecteurs indépendants, le LCR étant toujours la référence :

- **Global** : corrélation LFE ↔ L/C/R par fréquence de 5 à 150 Hz, sur la
  durée. Un LFE décorrélé n'alerte pas, quelle que soit sa phase. L'alerte peut
  être limitée à la phase négative.
- **Transitoires** : les attaques du mix sont détectées en pleine bande sur le
  downmix L+C+R ; pour chacune, le moteur dit si elle est aussi dans le LFE et,
  si oui, mesure le décalage du LFE (en avance / en retard, en ms, avec sa
  précision) et sa polarité, même sous une ligne de basse tenue.

Les deux fonctionnent de la même façon quel que soit le niveau, jusqu'à
−120 dBFS.

S'y ajoute un score de robustesse de 0 à 100 : le grave du mix est recalculé
sur 17 systèmes de lecture (cinéma avec sub décalé ou inversé ; bass
management ; stéréo sans LFE) et chaque écart est converti en
sensation d'après ISO 226. S'y ajoutent le niveau du LFE face à la façade, leur
recouvrement et la polarité cumulée de la séance.

Le détail, les formules et les références publiées sont dans
[`docs/ALGORITHME.md`](docs/ALGORITHME.md).

## Licence de JUCE

JUCE est distribué sous AGPLv3 ou sous licence commerciale (gratuite en dessous
d'un certain chiffre d'affaires). À vérifier avant toute diffusion de
l'application : <https://juce.com/get-juce/>.
