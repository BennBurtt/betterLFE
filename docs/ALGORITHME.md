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

## 3. Robustesse du grave : systèmes de lecture et score

**Question posée :** une fois sorti de l'auditorium, de combien le grave du mix
s'écarte-t-il de ce qui a été validé, selon la façon dont il est diffusé ?

### 3.1 Ce que disent les normes et la littérature

| Fait | Source | Conséquence dans l'algorithme |
|---|---|---|
| En salle, le canal sub est réglé 10 dB au-dessus d'un canal d'écran, dans sa bande | SMPTE RP 200 [9] | Le LFE est toujours compté à +10 dB |
| Le même signal envoyé au centre et au sub doit donner +3 dB dans la bande commune | SMPTE RP 200 [9] | La norme suppose une addition en puissance : la relation de phase sub / écran n'est pas garantie |
| La restitution varie largement d'une salle à l'autre, et surtout entre l'auditorium et la salle ; l'écart est sévère dans le grave | Rapport SMPTE TC-25CSS 2014 [10], Hill et al. [11] | Le retard et la polarité du sub sont simulés. Le niveau du sub ne l'est pas : il varie d'une salle à l'autre, et c'est précisément pourquoi le grave ne doit pas reposer sur le LFE |
| Le LFE est optionnel à la réception, ne doit porter que de l'information d'appoint, et ne doit pas porter tout le grave du programme | UIT-R BS.775 [12] | Un LFE qui porte l'essentiel du grave est un défaut du mix |
| Les downmix deux canaux omettent le LFE | Dolby [13], UIT-R BS.775 [12] | La stéréo sans LFE est un système de lecture à part entière |
| Dans le grave, les courbes d'isosonie sont resserrées : de petits écarts de niveau s'entendent beaucoup | ISO 226:2003 [14], Leembruggen & Newell [15] | Les dB sont convertis en sensation, bande par bande |
| Un écart de niveau commence à s'entendre vers 1 dB | psychoacoustique classique [16] | En dessous de 1 dB, aucune pénalité |

### 3.2 Des dB à la sensation (ISO 226:2003)

La sonie double tous les 10 phones. À 1 kHz cela fait 10 dB ; dans le grave,
bien moins. Calcul à partir de la formule et des paramètres de la norme, autour
de 80 phones :

| Fréquence | dB pour doubler (ou diviser par deux) la sensation |
|---|---|
| 25 Hz | 5,0 dB |
| 40 Hz | 5,5 dB |
| 63 Hz | 6,2 dB |
| 80 Hz | 6,5 dB |
| 100 Hz | 6,9 dB |
| 1 000 Hz | 10,0 dB |

**Un écart de 6 dB sur le grave est donc entendu comme un grave deux fois plus
fort ou deux fois plus faible.** C'est le fondement chiffré de la règle « plus
de 6 dB, c'est grave ».

### 3.3 Référence

Le mix tel qu'il est entendu en auditorium calibré : canaux d'écran, plus LFE à
+10 dB, sub à l'heure et en polarité. Tout est mesuré par rapport à elle.

### 3.4 Systèmes simulés

Chaque système est une paire de filtres, l'un appliqué à F (L+C+R), l'autre au
LFE, avant leur somme. La puissance de la somme se déduit des spectres et
inter-spectres déjà mesurés, sans refiltrer l'audio :

`P = |Gf|²·Sff + |Gl|²·Sll + 2·Re(Gf·conj(Gl)·Sfl)`

Le terme croisé que le hasard seul peut produire est d'abord retiré (effacé
entre 3σ et 2σ de cohérence) : des signaux sans rapport ne sont jamais déclarés
en interaction.

| Famille | Cas | Nombre |
|---|---|---|
| Cinéma | sub décalé de −5, −2,5, 0, +2,5, +5 ms × polarité normale ou inversée | 10 |
| Bass management | canaux d'écran croisés vers le sub (Linkwitz-Riley 4ᵉ ordre) à 60, 80, 100 Hz, LFE passe-bas 120 Hz ; avec et sans passe-bas raide à 120 Hz tenant lieu de filtre d'encodeur | 6 |
| Stéréo | `Lo = L + 0,707·C`, `Ro = R + 0,707·C`, sans LFE | 1 |

### 3.5 Note d'un système

Pour chaque système et chaque bande (20–30, 30–60, 60–90, 90–120 Hz) :

1. `écart = niveau du système − niveau de la référence`, en dB, borné à ±24 dB.
2. `pénalité de bande = borne((|écart| − 1 dB) / (D − 1 dB), 0, 1)`, avec `D` le
   nombre de dB qui double la sonie dans cette bande : 5,0 ; 5,5 ; 6,4 ; 6,9.
   Zéro jusqu'à 1 dB, 1 quand la bande est entendue deux fois plus forte ou
   deux fois plus faible.
3. `pénalité du système = Σ part de la bande × pénalité de bande`, la part étant
   celle de la bande dans le grave de la référence. Juger par bande évite
   qu'une annulation à 40 Hz soit masquée par du niveau à 100 Hz.

### 3.6 Score

Deux risques distincts, multipliés :

`robustesse = 100 × (1 − interaction) × (1 − dépendance)`

**Interaction** : la plus forte valeur de `pertinence × pénalité` parmi les
systèmes cinéma et bass management. C'est le pire cas, pas une moyenne : un mix
qui s'annule sur un système plausible n'est pas rendu sûr par les systèmes où
il survit.

| Système | Pertinence |
|---|---|
| Cinéma, sub à 0 ms (les deux polarités) | 1 |
| Cinéma, sub en retard (+2,5 et +5 ms) | 0,5 |
| Cinéma, sub en avance (−5 et −2,5 ms) | 0,25 |
| Bass management (les six cas) | 0,5 |

**Dépendance** : `0,76 × pénalité de la stéréo sans LFE`, c'est-à-dire de ce que
le grave perd partout où le LFE manque ou est mal réglé. Le plafond de 0,76
exprime qu'un grave entièrement dans le LFE est grave (la salle seule en
décide) sans valoir une annulation.

Les chaînes sont calculées sur une moyenne de 0,2 s : le score réagit en 0,7 à
0,8 s avec la fenêtre d'analyse de 1 s. Le chiffre affiché ne change que
lorsqu'il a bougé d'au moins 1,5 point.

### 3.7 Lecture du score et valeurs de référence

| Score | Sens |
|---|---|
| 100 | Aucun système ne déplace le grave de façon audible, et il ne repose pas sur le LFE |
| 50 | Dangereux mais passable : le pire système à l'heure déplace le grave à mi-chemin d'un doublement ou d'une division par deux de la sensation (environ 3,5 dB) |
| 20–25 | Tout le grave est dans le LFE : la salle seule en décide |
| 0 | Un système plausible annule le grave, ou le rend deux fois plus fort |

Vérifiées par les tests, sur bruit large bande :

| Mix | Robustesse |
|---|---|
| Façade seule, LFE muet | 100 |
| LFE sans rapport avec la façade, 10 dB sous elle à l'écoute | 100 |
| LFE 10 dB sous la façade, à 30 % copié sur elle | 95 |
| LFE sans rapport, aussi fort que la façade | 62 |
| LFE 10 dB sous la façade, à 60 % copié sur elle | 60 |
| LFE seul | 22 |
| LFE sans rapport, 6 dB au-dessus de la façade | 22 |
| LFE aussi fort que la façade, à 60 % copié sur elle | 17 |
| LFE 10 dB sous la façade, entièrement copié sur elle | 8 |
| LFE identique à la façade | 0 |
| LFE identique mais en retard de 5 ms | 0 |

Même 10 dB sous la façade, un LFE entièrement copié sur elle fait varier le
grave de près de 6 dB entre un sub en polarité et un sub inversé.

### 3.8 Ce qui est établi et ce qui est supposé

| Élément | Statut |
|---|---|
| LFE à +10 dB, omission du LFE en downmix, rôle d'appoint du LFE | Normes [9][12][13] |
| Conversion dB → sensation (5,0 à 6,9 dB par doublement) | Calculé depuis ISO 226:2003 [14] |
| Seuil de 1 dB | Ordre de grandeur admis [16] ; plus fin à fort niveau |
| Filtres de bass management (Linkwitz-Riley 24 dB/oct) | Modèle courant, variable selon les appareils |
| Filtre d'encodeur (Butterworth 8ᵉ ordre à 120 Hz) | Modèle : le filtre LFE de Dolby Digital Plus n'est pas public |
| Étendue des défauts de salle (±5 ms, polarité) | **Hypothèse.** Le rapport SMPTE constate de larges écarts mais ses chiffres ne sont pas publics |
| Pertinences des systèmes, plafond de dépendance (0,76) | **Hypothèse** d'ingénierie, suivant la consigne (0 ms ×2, avance ×0,5 ; LFE seul vers 20–25). Aucune statistique publiée sur la fréquence de ces défauts |
| Acoustique de la salle (modes, places) | Non simulée |

### 3.9 Autres témoins

**Niveau du LFE face à la façade.** Rapport des énergies sur 20–120 Hz, LFE
compté à +10 dB. Signalé au-delà de +6 dB : le grave dépend alors du réglage du
sub de chaque salle, qui varie. Ce défaut de réglage n'est pas simulé dans le
score ; il est la raison d'être de ce témoin.

**Recouvrement.** Niveau RMS du LFE et de la somme d'écran par bande (30–60,
60–90, 90–120 Hz). Les marques sous le spectre se réchauffent quand les deux
jouent à moins de 10 dB l'un de l'autre dans 60–120 Hz.

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
- Les systèmes de lecture sont des modèles électriques ; l'acoustique d'une
  salle n'est pas simulée. Voir 3.8 pour ce qui est établi et ce qui est supposé.

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
9. SMPTE RP 200, *Relative and Absolute Sound Pressure Levels for
   Motion-Picture Multichannel Sound Systems*.
10. SMPTE, *TC-25CSS B-Chain Frequency and Temporal Response Analysis of
    Theatres and Dubbing Stages*, 2014.
11. A. J. Hill, M. O. J. Hawksford, P. Newell, « Enhanced Wide-Area
    Low-Frequency Sound Reproduction in Cinemas », *J. Audio Eng. Soc.*, 2016.
12. UIT-R BS.775, *Multichannel stereophonic sound system with and without
    accompanying picture*, annexe sur le canal LFE.
13. Dolby Laboratories, *Dolby Digital Professional Encoding Guidelines* et
    *5.1-Channel Production Guidelines*.
14. ISO 226:2003, *Acoustics — Normal equal-loudness-level contours*.
15. G. Leembruggen, P. Newell, « Why does cinema sound quality mostly fail to
    realise its potential? », *Proc. Institute of Acoustics*, 2015.
16. E. Zwicker, H. Fastl, *Psychoacoustics: Facts and Models*, Springer
    (seuils différentiels de niveau).
