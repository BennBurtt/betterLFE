# Algorithme de SubGUARD

Le LCR est toujours la référence : tout est exprimé comme « ce que fait le LFE
par rapport au canal d'écran ». L'analyse porte sur la bande commune aux deux,
de 5 à 190 Hz (affichée de 5 à 150 Hz), après filtrage anti-repliement et
décimation vers ~500 Hz.

Tout est calculé sur des rapports ou en énergie logarithmique : les deux
détecteurs se comportent de la même façon à −10 dBFS et à −120 dBFS. Seul le
silence numérique est écarté.

Deux détecteurs indépendants tournent en parallèle.

## 1. Détecteur global (son tenu)

Répond à : *sur la durée, le LFE est-il lié au canal d'écran, et dans quel sens ?*

1. Spectres et inter-spectres de chaque canal d'écran X et du LFE, par FFT de
   segments fenêtrés (Hann), moyennés dans le temps : méthode de Welch [1].
2. Lissage au tiers d'octave par un noyau compact, puis pour chaque bande :

   | Grandeur | Formule |
   |---|---|
   | Cohérence | `γ² = |Sxy|² / (Sxx·Syy)` [2][3] |
   | Phase | `arg(Sxy)` |
   | Corrélation | `c = Re(Sxy) / √(Sxx·Syy)` |
   | Interaction | `s = 2·Re(Sxy) / (Sxx + Syy)` |

   `c` vaut `|γ|·cos(phase)` : +1 même signal en phase, −1 même signal inversé,
   0 décorrélé ou en quadrature. Elle ne dépend pas des niveaux : c'est la
   grandeur jugée par défaut, pour qu'un LFE très bas soit analysé comme un LFE
   fort. Un LFE décorrélé donne `c ≈ 0` quelle que soit sa phase : **la
   corrélation prime sur la phase**.

   `s` est la même grandeur pondérée par l'équilibre des niveaux (option
   « Weigh by level balance ») : la somme X + LFE change de `10·log10(1 + s)` dB.
3. Incertitude. La cohérence estimée sur `K` moyennes indépendantes n'est jamais
   nulle, même pour des signaux sans rapport [2]. `K` vaut la largeur de bande
   multipliée par la durée réellement moyennée ; cette durée est calculée comme
   une taille d'échantillon effective pondérée par l'énergie [4], pour tenir
   compte du fait qu'un son fort qui démarre écrase tout ce qui précède, et de
   la portion de la fenêtre d'analyse réellement occupée par le signal.
   L'écart-type de `c` sous l'hypothèse « signaux sans rapport » est
   `σ = 1/√(2K)`.
4. Alerte quand `|c|` dépasse le seuil réglé (0,5 par défaut) **et** `2,5σ`.
   En mode « Out of phase only », seules les valeurs négatives alertent.

Conséquence physique : il faut quelques cycles pour conclure. Un signal commun
est reconnu en 0,5 s au-dessus de 50 Hz, 1,5 s au-dessus de 20 Hz, 3 s
au-dessus de 8 Hz.

## 2. Détecteur de transitoires

Répond à : *quand le LCR attaque, le LFE attaque-t-il en même temps ?*

1. **Détection d'attaque** sur chaque canal d'écran, dans cinq bandes
   (10–190, 10–30, 30–50, 50–80, 80–190 Hz), pour qu'un boom à 25 Hz soit vu
   sous un fond plus fort à 100 Hz. Dans chaque bande, l'enveloppe d'énergie
   en dB (lissée sur environ une période) est comparée à son niveau récent
   (0,3 s) : c'est la différence relative d'enveloppe [5]. Le seuil s'adapte à
   la bande [6] : au moins 6 dB, et au moins 3,5 fois la fluctuation habituelle
   de cette bande, pour ne pas prendre les ondulations d'un grondement pour des
   attaques.
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
4. **Verdict**, seulement si la ressemblance des deux transitoires une fois
   réalignés dépasse le seuil (en mode « Out of phase only » : seulement si la
   corrélation sans réalignement est négative au-delà du seuil) :

   | Condition | Verdict |
   |---|---|
   | `σ_retard` > 8 ms | lié, mais décalage non mesurable |
   | `|retard|` > max(2,5 ms, 2·σ_retard) | LFE en retard / en avance de x ± σ ms |
   | aligné, phase < 60° | aligné, en phase |
   | aligné, phase > 120° | aligné, polarité inversée |
   | aligné, entre les deux | aligné, déphasé |

   2,5 ms est un quart de période à 100 Hz.

## 3. Chaînes de lecture, robustesse et témoins

Tout ce qui suit utilise le LFE **tel qu'il est entendu**, soit 10 dB au-dessus
d'un canal d'écran, quel que soit le réglage d'affichage.

**Chaînes de lecture.** Chaque chaîne est une paire de filtres, l'un appliqué à
F (L+C+R), l'autre au LFE, avant leur somme. La puissance de la somme se déduit
des spectres et inter-spectres déjà moyennés, sans refiltrer l'audio :

`P = |Gf|²·Sff + |Gl|²·Sll + 2·Re(Gf·conj(Gl)·Sfl)`

Elle est comparée à la **somme en puissance** (la même expression sans les
terme croisé), c'est-à-dire au grave qu'on aurait si le LFE et la façade
n'interagissaient pas. Plusieurs dB en dessous : annulation.

| Chaîne | Modèle |
|---|---|
| Cinéma | somme acoustique écran + LFE ; sub décalé de −5, −2,5, 0, +2,5, +5 ms ; polarité normale ou inversée |
| Bass management | canaux d'écran croisés vers le sub (Linkwitz-Riley 4ᵉ ordre à 60, 80 ou 100 Hz), LFE passe-bas 120 Hz ; somme électrique |
| Filtre d'encodeur | idem avec un passe-bas raide à 120 Hz sur le LFE (Butterworth 8ᵉ ordre). C'est un modèle : le filtre LFE réel de Dolby Digital Plus n'est pas public |

Le terme croisé que le hasard seul peut produire est retiré (effacé entre
4σ et 2σ de cohérence) : des signaux sans rapport donnent exactement la somme
en puissance.

**Robustesse (0 à 100).** Pour chacune des 16 chaînes (10 cinéma, 6 domestiques),
on a l'écart en dB entre son grave et la somme en puissance, sur 20–120 Hz.
Le score ne regarde que la distance entre la meilleure et la pire :
`100 × (1 − (max − min) / 12 dB)`, borné entre 0 et 100. 100 : toutes les
chaînes rendent le même grave. 0 : au moins 12 dB d'écart. Une annulation est
comptée au plus à −24 dB. Le score suit le réglage « Integration » et porte sur
ce qui joue à l'instant, pas sur le film entier.

**Dépendance au LFE.** Downmix stéréo du LCR sans LFE (`Lo = L + 0,707·C`,
`Ro = R + 0,707·C`) : part de l'énergie de grave portée par le LFE, et perte en
dB quand il disparaît. Signalé au-delà de 70 %.

**Recouvrement.** Niveau RMS du LFE et de la somme d'écran par bande (30–60,
60–90, 90–120 Hz). Les marques sous le spectre se réchauffent quand les deux
jouent à moins de 10 dB l'un de l'autre dans 60–120 Hz.

**Hors bande.** Énergie du LFE au-dessus de 120 Hz (passe-haut 4ᵉ ordre, mesuré
à pleine fréquence d'échantillonnage) rapportée au LFE entier. Signalé au-delà
de −20 dB.

**Polarité de séance.** Corrélation LFE ↔ L, C, R pondérée par l'énergie,
cumulée sans oubli depuis la dernière remise à zéro. Signalée sous −0,3 après
30 s. Elle reflète ce qui a été joué : rejouer une scène la compte plusieurs
fois.

## Ce qui est vérifié

`tests/EngineTests.cpp` contrôle sur signaux synthétiques : niveaux, repliement,
phase d'un retard connu, taux de fausses alertes sur signaux indépendants,
comportement identique 100 dB plus bas, lecture jusqu'à 5 Hz, modes d'alerte,
retards de transitoires retrouvés à 0,5 ms près, inversion de polarité, et
cohérence entre l'erreur réelle et l'incertitude annoncée.

Détection d'attaque, sur 79 coups espacés de 1,5 s, LFE = même coup retardé :

| Cas | Coups trouvés |
|---|---|
| Kick seul, y compris à −90 dBFS | tous |
| Kick sur un fond 20 ou 14 dB plus bas | tous |
| Kick sur un fond seulement 8 dB plus bas | environ 4 sur 10 |
| Boom 28 Hz seul, y compris à −90 dBFS | tous |
| Boom 28 Hz sous un fond 60–190 Hz | tous |
| Coup 45 Hz sous un fond 80–190 Hz plus fort | tous |
| Coup à 12 Hz | tous |
| Bruit tenu sans aucun coup | 1 à 5 fausses attaques par minute |

Une fausse attaque sur un bruit tenu ne produit un verdict que si le LFE lui
ressemble au-delà du seuil ; avec un LFE sans rapport, rien n'est affiché.

## Limites

- L'analyse porte sur le signal du mix, pas sur l'acoustique d'une salle.
- Les seuils (0,5 ; 2,5σ ; 6 dB ; 3,5× ; 2,5 ms) sont des choix de conception,
  pas des valeurs normalisées. Ils restent à ajuster sur de vrais mixes.
- Un transitoire qui dépasse à peine le fond dans toutes les bandes à la fois
  (moins de ~10 dB) peut ne pas être détecté comme attaque.
- Tout en bas (5–15 Hz), la résolution est limitée par la durée de la fenêtre :
  utiliser le mode « Ultra (2 s) » pour cette zone.
- Les surrounds ne sont pas analysés, ni redirigés dans les chaînes simulées.
- Les chaînes de lecture sont des modèles électriques ; l'acoustique d'une salle
  n'est pas simulée. Les seuils des témoins (70 %, −20 dB, −0,3, 12 dB) sont des
  choix de conception.

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
