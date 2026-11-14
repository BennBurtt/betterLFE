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

Logo et icône sont optionnels : voir `assets/README.md`.

## Organisation

| Dossier | Contenu |
|---|---|
| `core/` | Moteur d'analyse en C++17 pur, sans dépendance (`LfeEngine`, `AlertTracker`) |
| `app/` | Application JUCE : entrée audio, interface |
| `tests/` | Vérifications du moteur sur signaux synthétiques |

## Ce qui est mesuré

L'audio est filtré et décimé vers ~500 Hz, puis analysé par spectres et
inter-spectres moyennés dans le temps et lissés au tiers d'octave. Pour chaque
canal d'écran X face au LFE :

| Grandeur | Formule | Lecture |
|---|---|---|
| Interaction `s` | `2·Re(Sxy) / (Sxx + Syy)` | +1 en phase à niveau égal, −1 annulation totale, 0 aucune interaction. La somme X + LFE varie de `10·log10(1 + s)` dB |
| Cohérence | `|Sxy|² / (Sxx·Syy)` | 1 = même signal, 0 = décorrélé |
| Phase | `arg(Sxy)` | déphasage X / LFE |
| Incertitude `σ` | d'après la largeur de bande et la durée réellement moyennée | écart que le hasard seul peut produire sur `s` |

L'alerte se déclenche quand `|s|` dépasse le seuil réglé **et** 3 σ.
Ambre : en phase. Rouge : hors phase.

## Licence de JUCE

JUCE est distribué sous AGPLv3 ou sous licence commerciale (gratuite en dessous
d'un certain chiffre d'affaires). À vérifier avant toute diffusion de
l'application : <https://juce.com/get-juce/>.
