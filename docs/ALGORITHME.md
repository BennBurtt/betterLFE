# Algorithme de SubGUARD

Le LCR est toujours la référence : tout est exprimé comme « ce que fait le LFE
par rapport au canal d'écran ». L'analyse porte sur la bande commune aux deux,
de 15 à 190 Hz, après filtrage anti-repliement et décimation vers ~500 Hz.

Deux détecteurs indépendants tournent en parallèle.

## 1. Détecteur global (son tenu)

Répond à : *sur la durée, le LFE est-il lié au canal d'écran, et dans quel sens ?*

1. Spectres et inter-spectres de chaque canal d'écran X et du LFE, par FFT de
   segments fenêtrés (Hann), moyennés dans le temps : méthode de Welch [1].
2. Lissage au tiers d'octave par un noyau compact, puis pour chaque bande :

   | Grandeur | Formule |
   |---|---|
   | Cohérence (corrélation par fréquence) | `γ² = |Sxy|² / (Sxx·Syy)` [2][3] |
   | Phase | `arg(Sxy)` |
   | Interaction | `s = 2·Re(Sxy) / (Sxx + Syy)` |

   `s` vaut `|γ|·cos(phase)` pondéré par l'équilibre des niveaux. La somme
   X + LFE change de `10·log10(1 + s)` dB : +1 en phase à niveau égal, −1
   annulation, 0 aucune interaction. Un LFE décorrélé donne `s ≈ 0` quelle que
   soit sa phase : **la corrélation prime sur la phase**.
3. Incertitude. La cohérence estimée sur `K` moyennes indépendantes n'est jamais
   nulle, même pour des signaux sans rapport [2]. `K` vaut la largeur de bande
   multipliée par la durée réellement moyennée ; cette durée est calculée comme
   une taille d'échantillon effective pondérée par l'énergie [4], pour tenir
   compte du fait qu'un son fort qui démarre écrase tout ce qui précède.
   L'écart-type de `s` sous l'hypothèse « signaux sans rapport » est
   `σ = 1/√(2K)`.
4. Alerte quand `|s|` dépasse le seuil réglé (0,5 par défaut) **et** `3σ`.

## 2. Détecteur de transitoires

Répond à : *quand le LCR attaque, le LFE attaque-t-il en même temps ?*

1. **Détection d'attaque** sur chaque canal d'écran : l'enveloppe d'énergie
   rapide (20 ms) est comparée au niveau récent (250 ms). Une attaque est
   déclarée quand elle le dépasse de 6 dB. C'est la détection par différence
   relative d'enveloppe [5][6].
2. **Mesure du décalage** : corrélation croisée entre le segment du canal
   d'écran (300 ms autour de l'attaque) et le LFE, pour tous les décalages de
   −50 à +50 ms. C'est la corrélation croisée généralisée non pondérée [7]. On
   utilise sa version analytique : le maximum de son enveloppe situe le
   transitoire du LFE dans le temps, et la phase à ce maximum donne la polarité.
   Cela distingue un LFE *inversé* d'un LFE *en retard d'une demi-période*, que
   la corrélation simple confond sur un son grave.
3. **Précision du décalage**, d'après la borne de Cramér-Rao de l'estimation de
   retard [8] :

   `σ_retard = √((1 − m²) / (m²·N)) / (2π·β)`

   avec `m` la ressemblance des deux transitoires, `β` la largeur de bande
   efficace qu'ils partagent et `N = 2·B·T` le nombre d'échantillons
   indépendants du segment. Un coup à bande étroite ou noyé dans autre chose
   reçoit une grande incertitude.
4. **Verdict**, seulement si l'interaction une fois réaligné dépasse le seuil :

   | Condition | Verdict |
   |---|---|
   | `σ_retard` > 8 ms | lié, mais décalage non mesurable |
   | `|retard|` > max(2,5 ms, 2·σ_retard) | LFE en retard / en avance de x ± σ ms |
   | aligné, phase < 60° | aligné, en phase |
   | aligné, phase > 120° | aligné, polarité inversée |
   | aligné, entre les deux | aligné, déphasé |

   2,5 ms est un quart de période à 100 Hz.

## Ce qui est vérifié

`tests/EngineTests.cpp` contrôle sur signaux synthétiques : niveaux, repliement,
phase d'un retard connu, taux de fausses alertes sur signaux indépendants,
retards de transitoires retrouvés à 0,5 ms près, inversion de polarité, et
cohérence entre l'erreur réelle et l'incertitude annoncée.

## Limites

- L'analyse porte sur le signal du mix, pas sur l'acoustique d'une salle.
- Les seuils (0,5 ; 3σ ; 6 dB ; 2,5 ms) sont des choix de conception, pas des
  valeurs normalisées. Ils restent à ajuster sur de vrais mixes.
- Un transitoire à moins de ~14 dB au-dessus du fond sonore peut ne pas être
  détecté comme attaque.
- Les surrounds ne sont pas analysés.

## Références

1. P. D. Welch, « The use of fast Fourier transform for the estimation of power
   spectra », *IEEE Trans. Audio Electroacoust.*, vol. 15, 1967.
2. G. C. Carter, C. H. Knapp, A. H. Nuttall, « Estimation of the
   magnitude-squared coherence function via overlapped fast Fourier transform
   processing », *IEEE Trans. Audio Electroacoust.*, vol. 21, 1973.
3. J. S. Bendat, A. G. Piersol, *Random Data: Analysis and Measurement
   Procedures*, Wiley.
4. L. Kish, *Survey Sampling*, Wiley, 1965 (taille d'échantillon effective).
5. A. Klapuri, « Sound onset detection by applying psychoacoustic knowledge »,
   *Proc. IEEE ICASSP*, 1999.
6. J. P. Bello et al., « A tutorial on onset detection in music signals »,
   *IEEE Trans. Speech Audio Process.*, vol. 13, 2005.
7. C. H. Knapp, G. C. Carter, « The generalized correlation method for
   estimation of time delay », *IEEE Trans. Acoust., Speech, Signal Process.*,
   vol. 24, 1976.
8. G. C. Carter, « Coherence and time delay estimation », *Proc. IEEE*,
   vol. 75, 1987.
