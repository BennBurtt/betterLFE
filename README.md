# betterLFE — SubGUARD

Analyseur temps réel de l'interaction entre le LFE et les canaux d'écran
(L / C / R) entre 20 et 150 Hz, pour le mixage cinéma.

## Lancer

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
python main.py
```

## Compiler l'application macOS

```bash
./build_mac.sh
```

Le résultat est `dist/SubGuard.app`. `logo.png` et `SubGuard.icns` sont
optionnels : posés à la racine du dépôt, ils sont intégrés automatiquement.

## Ce qui est mesuré

Pour chaque canal d'écran X et le LFE, le moteur (`betterlfe/dsp.py`)
maintient des spectres et inter-spectres moyennés dans le temps et lissés
au tiers d'octave, et en tire :

| Grandeur | Formule | Lecture |
|---|---|---|
| Interaction `s` | `2·Re(Sxy) / (Sxx + Syy)` | +1 en phase à niveau égal, −1 annulation totale, 0 aucune interaction. La somme X + LFE varie de `10·log10(1 + s)` dB |
| Cohérence | `|Sxy|² / (Sxx·Syy)` | 1 = même signal, 0 = décorrélé |
| Phase | `arg(Sxy)` | déphasage X / LFE |
| Incertitude `σ` | d'après la bande et la durée réellement moyennée | écart que le hasard seul peut produire sur `s` |

L'alerte se déclenche quand `|s|` dépasse le seuil réglé **et** 2,5 σ.
Ambre : en phase. Rouge : hors phase.

## Tests

```bash
pip install -r requirements-dev.txt
python -m pytest tests -q
```

`legacy/subguard_v1.py` est l'application d'origine, gardée pour référence.
