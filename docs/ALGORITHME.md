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
   | Cohérence | `γ² = abs(Sxy)² / (Sxx·Syy)` [2][3] |
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
4. Alerte quand `|c|` dépasse le seuil choisi **et** `2,5σ`. Trois niveaux, par
   le sélecteur au-dessus du spectre : Soft (0,90), Medium (0,75), Strong
   (0,50, par défaut). En mode « Out of
   phase only », seules les valeurs négatives alertent.
5. Quand le LFE est absent (niveau RMS sous -100 dBFS pendant 0,5 s), tout ce qui
   a été mesuré face à lui est effacé : la courbe et les alertes repartent de
   zéro au lieu de garder la trace d'un son terminé. Un LFE très bas, à
   −80 dBFS par exemple, reste analysé normalement.

   Dans `s`, le LFE est pesé contre **toute la façade** et non contre le seul
   canal comparé (`Sxx` y est remplacé par la puissance de L+C+R quand elle est
   plus grande) : un LFE aussi fort qu'un centre discret, sous un gauche et un
   droit qui portent le grave, compte peu.

Conséquence physique : il faut quelques cycles pour conclure. Un signal commun
est reconnu en 0,5 s au-dessus de 50 Hz, 1,5 s au-dessus de 20 Hz, 3 s
au-dessus de 8 Hz.

## 2. Moteur de transitoires

Répond à : *quand le mix attaque, le LFE attaque-t-il aussi, et au même
instant ?* La référence est le **downmix des canaux d'écran** (Lo + Ro, soit
L + R + 1,414·C), en pleine bande : ce que tout le monde entend, quel que soit
le système. Un événement est produit pour chaque attaque du mix ; il dit si
elle est aussi dans le LFE, et si oui comment le LFE se place.

1. **Attaques du mix, pleine bande.** Trois vues du downmix, à la fréquence
   d'échantillonnage d'entrée ; la première qui se déclenche l'emporte (garde réglée par
   « Hold », 150 ms par défaut : un kick, son clic et sa cymbale ne font
   qu'une attaque).
   - *Crêtes au-dessus de 8 kHz* et *crêtes au-dessus de 600 Hz* : la crête
     est relevée par blocs de 1 ms, et une attaque est déclarée quand la crête
     des 12 dernières ms dépasse d'au moins 6 dB (un doublement) la plus haute
     crête des 36 ms précédentes, et d'au moins 4 fois la variation habituelle
     du signal. C'est la détection par crêtes de sous-blocs des codeurs audio
     (AC-3 [18]) et par énergie haute fréquence, comparées dans [21], et le
     test « vallée → crête » d'AudioLib [22] (qui demande un rapport de 1,5 ;
     AC-3 demande 2,5 à sa couche la plus large). Comparer à ce qui précède
     immédiatement, et non à une moyenne longue, permet de voir un charleston
     un quart de seconde après un kick. La vue au-dessus de 8 kHz isole un
     clic ou un charleston de tout le reste.
   - *Énergie des bandes graves* (10–40, 40–150, 150–600 Hz) : l'enveloppe
     d'énergie en dB, lissée sur environ une période, est comparée à son
     niveau récent (0,3 s) : différence relative d'enveloppe [5]. Le seuil
     s'adapte à la bande [6] : au moins 6 dB et au moins 5,5 fois sa
     fluctuation habituelle. Cette vue trouve un boom sans clic.
2. **L'attaque est-elle dans le LFE ?** Le même détecteur d'énergie tourne sur
   le LFE (10–190, 10–30, 30–50, 50–80, 80–190 Hz). L'attaque du mix compte
   comme présente dans le LFE si le LFE a sa propre attaque dans la plage de
   recherche (±250 ms par défaut, réglable), ou si son énergie y monte d'au moins 1 dB avec une forme
   qui ressemble à celle du mix (ressemblance ≥ 0,7).
   Une attaque du LFE est attribuée à l'attaque du mix la plus proche d'elle :
   un charleston placé entre deux kicks ne « prend » pas le LFE du kick voisin.
3. **Retard d'attaque à attaque.** Le début de chaque attaque est l'instant où
   le signal franchit un cinquième du chemin entre ce qui précédait et sa
   crête. La différence entre le début de l'attaque du LFE et celui de
   l'attaque du mix donne un retard grossier (quelques ms), qui **ne demande
   aucune ressemblance** entre les deux sons. C'est lui qui est affiché quand
   le LFE ne porte pas le même son que le mix (envoi très filtré, générateur
   de sous-harmoniques) : « LFE attack late ≈ 196 ms ».
4. **Retard fin et polarité, par corrélation.** Utilisé quand la ressemblance
   dépasse le réglage « Likeness needed for a phase reading » (0,3 par
   défaut). Le mix et le LFE n'ont pas la même
   largeur de bande, et une basse tenue peut sonner dans les deux. Avant de
   les comparer :
   - le mix est ramené à la bande du LFE (8–190 Hz) : on ne compare deux
     observations d'un même événement qu'après leur avoir donné la même
     réponse. C'est le principe de la soustraction d'images en astronomie et
     de son extension aux sources qui se déplacent (« Translient ») [23][24],
     transposé de la position au temps ;
   - les deux signaux passent par **un seul et même filtre blanchisseur**,
     ajusté par prédiction linéaire (ordre 8) sur les 300 ms qui précèdent
     l'attaque : ce qui sonnait déjà (une note de basse) est retiré des deux.
     Le filtre étant commun, il ne décale aucun des deux signaux par rapport à
     l'autre. C'est le préfiltrage de la corrélation croisée généralisée [7] ;
   - la même analyse est faite sur les 300 ms qui précèdent et **soustraite**
     (nouveau moins référence, comme en soustraction d'images) : il ne reste
     que ce que l'attaque a apporté.

   Vient alors la corrélation croisée entre le segment du mix (300 ms autour
   de l'attaque) et le LFE, pour tous les décalages de la plage de recherche [7]. On
   utilise sa version analytique : le maximum de son enveloppe situe le
   transitoire du LFE dans le temps, et la phase à ce maximum donne la polarité.
   Cela distingue un LFE *inversé* d'un LFE *en retard d'une demi-période*, que
   la corrélation simple confond sur un son grave. Parmi les décalages qui
   correspondent presque aussi bien que le meilleur (à 5 % près), le plus petit
   est retenu : un coup à bande étroite donne un maximum large et ondulé.
5. **Précision du décalage**, d'après la borne de Cramér-Rao de l'estimation de
   retard [8] :

   `σ_retard = √((1 − m²) / (m²·N)) / (2π·β)`

   avec `m` la ressemblance telle qu'elle est dans le segment, avec tout ce
   qui y sonne d'autre, `β` la largeur de bande
   efficace qu'ils partagent et `N = 2·B·T` le nombre d'échantillons
   indépendants du segment. Un coup à bande étroite ou noyé dans autre chose
   reçoit une grande incertitude. Cette borne est un minimum théorique : sur un
   signal très étroit ou tout en bas du spectre (sous 30 Hz, où une période
   dépasse 33 ms), l'erreur réelle peut la dépasser, et un décalage de quelques
   millisecondes n'y a de toute façon guère de sens à l'écoute.
6. **Verdict fin**, pour les attaques présentes dans le LFE, seulement si la ressemblance des deux transitoires une fois
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

Le bandeau « Transients » affiche le verdict de la dernière attaque du mix :
verdict fin si la ressemblance suffit ; sinon retard d'attaque à attaque ;
sinon « nothing in the LFE within ±x ms ».

**Activation.** Le contrôle des transitoires est éteint par défaut (case
« Transient check on » du panneau « Transients »). Aucun détecteur n'est
parfait sur un mix complet : l'usage prévu est de mettre en solo dans le DAW
les éléments à vérifier (un kick et son envoi LFE), puis d'allumer le contrôle.

**Réglages** (panneau « Transients »). Les quatre premiers sont ceux d'un
modeleur de transitoires, en pourcentage ; la valeur réelle est affichée à côté.

| Réglage | Défaut | Effet |
|---|---|---|
| Attack | 40 % (12 ms) ; 3 ms à 100 ms | Vitesse de montée exigée : durée sur laquelle la crête est observée. Bas : seulement les coups secs. Haut : les montées lentes comptent aussi |
| Hold | 25 % (150 ms) ; 30 à 510 ms | Après un transitoire, aucun nouveau pendant ce temps |
| Release | 50 % (200 ms) ; 20 ms à 2 s | Durée pendant laquelle la queue d'un coup masque ce qui suit : rien n'est un nouveau transitoire s'il ne dépasse pas cette queue, qui décroît à cette constante de temps. Haut : une queue qui résonne n'est pas prise pour des attaques. Bas : on retrouve les coups faibles qui suivent de près un coup fort |
| Sensitivity | 70 % (6 dB) ; 100 % = 2,6 dB, 40 % = 14 dB | Montée minimale qui compte comme transitoire, pour les crêtes et pour les bandes graves ; règle aussi l'exigence « dépasser la variation habituelle du signal » |
| Search range | ±250 ms (5 à 500) | Jusqu'où le LFE est cherché avant et après l'attaque. Large : trouve un LFE très en retard. Étroit : plus précis sur une musique dense et sur les coups faibles |
| Likeness needed for a phase reading | 0,3 | Ressemblance à partir de laquelle le retard fin et la polarité sont affichés |
| Display time | 4 s | Durée d'affichage d'un verdict |

**Attribution.** Une attaque du LFE revient à l'attaque du mix la plus proche,
et une attaque du mix qui *suit* le LFE compte comme quatre fois plus loin : un
envoi arrive après sa source bien plus souvent qu'avant. Sans cela, ce qui est
détecté dans la queue d'un coup s'attribuait le LFE de ce coup et le lisait
« en avance ».

## 3. Robustesse du grave : systèmes de lecture et score

> **Ce que le score garantit, et ce qu'il ne garantit pas.** Les systèmes de
> lecture sont des modèles *électriques* : retard, polarité, filtrage, niveau.
> Un score de 100 dit que le mix résiste à ces écarts et au bass management
> courant. Il ne dit rien de l'acoustique d'une salle : sous la fréquence de
> Schroeder, les modes propres creusent la réponse de 15 à 20 dB à certaines
> places, pour la façade comme pour le caisson, quel que soit le mix.

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
| Cinéma | sub trop près ou trop loin de 0,9 m et 1,7 m (soit ±2,5 et ±5 ms à 343 m/s), ou en place × polarité normale ou inversée | 10 |
| Bass management | canaux d'écran croisés vers le sub (Linkwitz-Riley 4ᵉ ordre) à 60, 80, 100 Hz, LFE passe-bas 120 Hz ; avec et sans passe-bas raide à 120 Hz tenant lieu de filtre d'encodeur | 6 |
| Stéréo | le mix sans son LFE, comme dans tout downmix deux canaux. Seul le LFE est retiré : la façade reste sommée comme dans la référence, pour que la façon dont L, C et R se replient (qui ne dépend pas du LFE) ne compte pas comme une perte | 1 |

**Dépendance au niveau du sub (« LFE level dependence »).** Un avertissement,
qui retire aussi jusqu'à 15 % du score. Chacun des 16 systèmes cinéma et bass management est recalculé avec son
sub réglé à ±1, ±2, ±3 et ±6 dB. Avec un LFE sans rapport avec la façade, le
grave suit simplement la somme des puissances : c'est attendu, tout comme un
grave qui monte de 1 dB quand il est entièrement dans un LFE joué 1 dB plus
fort. Ce qui est signalé est l'écart à cette attente, bande par bande (bandes
portant au moins 20 % du grave) :

`écart = variation réelle − variation attendue d'un LFE sans rapport`

La valeur affichée est la **moyenne** de cet écart sur tous les cas calculés
(16 systèmes × 8 niveaux, la bande la plus touchée de chaque cas), avec le
**maximum** entre parenthèses.

Un écart négatif est une annulation qui s'installe, un écart positif un grave
qui sort d'une annulation. La case passe de l'orange clair au rouge ; rouge
quand la moyenne atteint 3 dB. Un cas à ±6 dB compte pour 0,3 dans la moyenne.

Les niveaux négatifs sont gardés : avec un LFE plus fort que la façade et en
opposition, *baisser* le sub rapproche les deux de l'annulation.

### 3.5 Note d'un système

Pour chaque système et chaque bande (20–30, 30–60, 60–90, 90–120 Hz) :

1. `écart = niveau du système − niveau de la référence`, en dB, borné à ±24 dB.
2. `pénalité de bande = borne((|écart| − T) / (D − T), 0, 1)`, avec `D` le
   nombre de dB qui double la sonie dans cette bande (5,0 ; 5,5 ; 6,4 ; 6,9) et
   `T` la tolérance du niveau choisi (voir 3.6). Zéro jusqu'à `T`, 1 quand la
   bande est entendue deux fois plus forte ou deux fois plus faible.
3. `pénalité du système = Σ part de la bande × pénalité de bande`, la part étant
   celle de la bande dans le grave de la référence. Juger par bande évite
   qu'une annulation à 40 Hz soit masquée par du niveau à 100 Hz.

### 3.6 Score

Deux risques distincts, multipliés, plus une légère retenue pour la dépendance au niveau du sub (15 % au plus, voir 3.4) :

`robustesse = 100 × (1 − interaction) × (1 − dépendance) × (1 − 0,15 × dépendance au niveau)`

**Interaction** : la plus forte valeur de `pertinence × pénalité` parmi les
systèmes cinéma et bass management. C'est le pire cas, pas une moyenne : un mix
qui s'annule sur un système plausible n'est pas rendu sûr par les systèmes où
il survit.

| Système | Pertinence |
|---|---|
| Cinéma, sub à 0 ms (les deux polarités) | 1 |
| Cinéma, sub en retard (+2,5 et +5 ms) | 0,5 |
| Cinéma, sub en avance (−5 et −2,5 ms) | 0,25 |
| Bass management (les six cas) | 1 |

**Dépendance** : `0,89 × pénalité de la stéréo sans LFE`, c'est-à-dire de ce que
le grave perd partout où le LFE manque ou est mal réglé. Le plafond de 0,89
place un grave large bande entièrement dans le LFE vers 22 : grave (la salle
seule en décide) sans valoir une annulation.

**L'infra-grave est mis à part.** Ce que la stéréo perd est pondéré par la
fréquence : un quart en dessous de 25 Hz, tout à partir de 60 Hz, et entre les
deux un poids qui monte linéairement en octaves. Une sinusoïde de 20 Hz seule
dans le LFE coûte donc peu : on ne la ressent qu'en salle, et l'enlever change
peu le downmix stéréo. Elle garde un quart de son poids, pour ne pas compter
pour rien.

| Grave présent uniquement dans le LFE | Dépendance | Score |
|---|---|---|
| Sinusoïde à 20 Hz | 0,22 | 78 |
| 30 Hz | 0,37 | 63 |
| 40 Hz | 0,59 | 41 |
| 50 Hz | 0,76 | 24 |
| 70 Hz et plus | 0,89 | 11 |
| Bruit large bande 20–120 Hz | — | 21 |

Les bornes viennent de trois constats (voir 3.8 bis) : le seuil d'audition
monte très vite vers le bas (78,5 dB SPL à 20 Hz, 68,7 dB à 25 Hz, 37,5 dB à
63 Hz [14]) ; sous 20–25 Hz la sensation passe de l'écoute au ressenti
corporel [25] ; et les appareils sur lesquels s'écoute un downmix ne
descendent pas là : un téléviseur qui atteint 60 Hz est déjà un bon
téléviseur, un petit haut-parleur coupe vers 150 Hz [26].

Les chaînes sont calculées sur une moyenne de 0,2 s : le score réagit en 0,7 à
0,8 s avec la fenêtre d'analyse de 1 s. Le chiffre affiché ne change que
lorsqu'il a bougé d'au moins 1,5 point.

**Niveau de sévérité.** Le sélecteur au-dessus du spectre règle à la fois le
seuil de l'alerte de phase et la tolérance `T` du score :

| Niveau | Alerte de phase à partir de | Tolérance du score | Effet |
|---|---|---|---|
| Soft | corrélation 0,90 | 3 dB | Le plus indulgent : un mix sain reste à 100 |
| Medium | 0,75 | 2 dB | 100 possible, rarement longtemps |
| Strong | 0,50 | 1 dB, vers le plus petit écart audible | Le plus sévère : 100 exceptionnel sur du contenu réel |

Strong est le réglage d'origine de l'app, et le niveau par défaut.

Les repères ne dépendent pas du niveau : LFE identique à la façade 0, LFE seul
vers 22.

### 3.7 Lecture du score et valeurs de référence

| Score | Sens |
|---|---|
| 100 | Aucun système ne déplace le grave de façon audible, et il ne repose pas sur le LFE |
| 50 | Dangereux mais passable : le pire système à l'heure déplace le grave à mi-chemin d'un doublement ou d'une division par deux de la sensation (environ 3,5 dB) |
| 20–25 | Tout le grave est dans le LFE : la salle seule en décide |
| 0 | Un système plausible annule le grave, ou le rend deux fois plus fort |

Vérifiées par les tests, sur bruit large bande, au niveau Strong :

| Mix | Robustesse |
|---|---|
| Façade seule, LFE muet | 100 |
| LFE sans rapport avec la façade, 10 dB sous elle à l'écoute | 100 |
| LFE 10 dB sous la façade, à 30 % copié sur elle | 93 |
| LFE sans rapport, aussi fort que la façade | 60 |
| LFE 10 dB sous la façade, à 60 % copié sur elle | 57 |
| LFE seul | 21 |
| LFE sans rapport, 6 dB au-dessus de la façade | 21 |
| LFE aussi fort que la façade, à 60 % copié sur elle | 18 |
| LFE 10 dB sous la façade, entièrement copié sur elle | 7 |
| LFE identique à la façade | 0 |
| LFE identique mais en retard de 5 ms | 0 |

Même 10 dB sous la façade, un LFE entièrement copié sur elle fait varier le
grave de près de 6 dB entre un sub en polarité et un sub inversé.

### 3.8 Ce qui est établi et ce qui est supposé

| Élément | Statut |
|---|---|
| LFE à +10 dB, omission du LFE en downmix, rôle d'appoint du LFE | Normes [9][12][13] |
| Conversion dB → sensation (5,0 à 6,9 dB par doublement) | Calculé depuis ISO 226:2003 [14] |
| Tolérances des trois niveaux (3 ; 2 ; 1 dB) | Choix ; 1 dB est l'ordre de grandeur admis du plus petit écart audible [16] |
| Filtres de bass management (Linkwitz-Riley 24 dB/oct) | Modèle courant, variable selon les appareils |
| Filtre d'encodeur (Butterworth 8ᵉ ordre à 120 Hz) | Modèle : le filtre LFE de Dolby Digital Plus n'est pas public |
| Étendue des défauts de salle (±5 ms, polarité) | **Hypothèse.** Le rapport SMPTE constate de larges écarts mais ses chiffres ne sont pas publics |
| Pertinences des systèmes, plafond de dépendance (0,89), bornes et poids de l'infra-grave (25 et 60 Hz, un quart), poids de la dépendance au niveau (15 %) | **Hypothèse** d'ingénierie, suivant la consigne (0 ms ×2, avance ×0,5 ; LFE seul vers 20–25). Aucune statistique publiée sur la fréquence de ces défauts |
| Acoustique de la salle (modes, places) | Non simulée |

### 3.8 bis Dépendance au LFE : ce que dit la littérature

| Source | Ce qu'elle établit |
|---|---|
| UIT-R BS.775 [12] | Le LFE est optionnel à la réception et ne doit porter que de l'information d'appoint, pas tout le grave du programme |
| AES TD1009 [17] | Le LFE est en général omis des downmix automatiques ; éviter d'y placer seul un contenu essentiel |
| ATSC A/52 (AC-3, E-AC-3) [18], Dolby [13] | Le downmix deux canaux réunit tous les canaux sauf le LFE |
| EBU Tech 3343 / UIT-R BS.1770 [19] | Le LFE est exclu de la mesure de sonie : un grave placé dans le LFE échappe au contrôle de loudness |
| Netflix, spécifications de mixage [20] | Un stéréo fabriqué à la main peut inclure le LFE, replié entre −8 et −12 dB et filtré à 200 Hz ou moins |

| ISO 226:2003 [14] | Seuil d'audition : 78,5 dB SPL à 20 Hz, 68,7 dB à 25 Hz, 37,5 dB à 63 Hz |
| Møller & Pedersen 2004 [25] | Sous 20 Hz environ, le son perd son caractère tonal ; le ressenti corporel (vibration) apparaît 20 à 25 dB au-dessus du seuil d'audition |
| Mesures de téléviseurs et de petites enceintes [26] | Extension dans le grave : 60 Hz est déjà une bonne valeur pour un téléviseur ; les petits haut-parleurs coupent vers 150 Hz |

Les premières vont dans le même sens : ce qui n'existe que dans le LFE peut manquer.
Mesurer la dépendance par ce que perd la stéréo sans LFE est donc la traduction
directe de ces textes. Aucune ne donne de seuil chiffré (« à partir de quelle
part le LFE porte trop »), ni de statistique sur la proportion de spectateurs
écoutant sans caisson : le plafond de 0,89 reste un choix.

Le repli manuel décrit par Netflix n'est pas simulé : à −10 dB, le LFE y arrive
20 dB sous son niveau de salle, ce qui change peu le résultat.

### 3.9 Autres témoins

**Niveau du LFE face à la façade.** Rapport des énergies sur 20–120 Hz, LFE
compté à +10 dB. Signalé au-delà de +6 dB : le grave dépend alors du réglage du
sub de chaque salle, qui varie. Ce défaut de réglage n'est pas simulé dans le
score ; il est la raison d'être de ce témoin.

**Recouvrement.** Niveau RMS du LFE et de la somme d'écran par bande (30–60,
60–90, 90–120 Hz). Les marques sous le spectre se réchauffent quand les deux
jouent à moins de 10 dB l'un de l'autre dans 60–120 Hz.

## 4. Inertie d'affichage

Les chiffres et les courbes affichés (score, tableau des systèmes, niveaux,
courbes de corrélation) passent par un filtre qui regarde depuis combien de
temps la valeur reste du même côté de ce qui est affiché :

- elle repasse sans cesse de part et d'autre (elle papillonne) : le filtre est
  lent (constante de temps 1,5 s) et l'affichage se pose sur la moyenne ;
- elle reste du même côté (un vrai changement) : après 0,15 s le filtre
  accélère, et à 0,6 s il tourne à pleine vitesse (0,12 s).

Mesuré : une valeur qui oscille entre 0,1 et 0,9 dix fois par seconde fait
bouger l'affichage de 0,03 ; un saut de 1 à 2 est suivi à 90 % en 0,7 s.

La frise du bas montre les 2 min 30 dernières (une colonne par demi-seconde).
Sa ligne « Robustness » est toujours colorée tant qu'il y a du grave : vert
pour un score de 100, orange à 50, rouge à 0, en dégradé continu.

## Ce qui est vérifié

`tests/EngineTests.cpp` contrôle sur signaux synthétiques : niveaux, repliement,
phase d'un retard connu, taux de fausses alertes sur signaux indépendants,
comportement identique 100 dB plus bas, lecture jusqu'à 5 Hz, modes d'alerte,
retards de transitoires retrouvés à 0,5 ms près, inversion de polarité, et
cohérence entre l'erreur réelle et l'incertitude annoncée.

Moteur de transitoires, sur une scène de type musique (ligne de basse à 55 Hz
dans L, R et le LFE, kick au centre sur le temps, charleston à contretemps,
40 s, 80 kicks et 80 charlestons) :

| Cas | Résultat |
|---|---|
| Attaques du mix trouvées, Release à 10 % | les kicks et la plupart des charlestons ; à 50 % (défaut), les kicks seuls |
| LFE portant le kick 12 ms en retard | 79 attaques dans le LFE, aucun charleston ; décalage médian +12,0 ms (80 % entre +11,6 et +12,4) |
| Kick du LFE à l'heure, ou inversé | 0,0 ms (±0,4) ; polarité inversée reconnue |
| Kick du LFE 6 ms en avance | −6,0 ms (±0,4) |
| Kick 12 dB plus faible sous la même basse, plage ±50 ms | 20 kicks sur 24 reconnus dans le LFE, +12,0 ms |
| Le même, plage ±250 ms | 4 sur 24 : une plage large coûte les coups faibles |
| LFE ne portant que la basse | aucune attaque dans le LFE |

Kick suivi d'une cymbale qui résonne en ondulant de 6 dB, LFE 200 ms en
retard, quatre coups : 4 attaques trouvées, 4 lues « en retard », aucune « en
avance ». Scène musique : 24 attaques (les kicks) avec Release à 50 %, 46
(kicks et charlestons) avec Release à 10 %.

Envoi LFE qui ne ressemble pas à sa source (kick au-dessus de 60 Hz au centre,
coup à 30 Hz dans le LFE 200 ms plus tard) : 4 attaques sur 4 datées, erreur
maximale 2 ms, ressemblance 0,22. Avec une plage de ±50 ms, rien n'est
apparié.

Sur coups isolés : kick retrouvé à 0,5 ms près, y compris à −90 dBFS et avec
un LFE 30 dB plus bas ; boom à 28 Hz sous un fond 60–190 Hz plus fort retrouvé
à ±1,5 ms avec une incertitude annoncée de 4 ms.

Fausses attaques sur un son tenu sans aucun coup, en 2 minutes (hors début
du son) : bruit blanc ou rose, aucune ; grondement grave (sous 90 Hz), 1 à 3.

## Limites

- L'analyse porte sur le signal du mix, pas sur l'acoustique d'une salle.
- Les seuils (0,5 ; 2,5σ ; 6 dB ; 4× et 5,5× ; 2,5 ms) sont des choix de conception,
  pas des valeurs normalisées. Ils restent à ajuster sur de vrais mixes.
- Un transitoire qui dépasse à peine le fond dans toutes les vues à la fois
  (moins de 6 dB en crête, moins de ~10 dB dans les bandes graves) peut ne pas
  être détecté comme attaque.
- Une attaque du mix sans grave propre (clic seul à l'écran, corps du coup
  uniquement dans le LFE) est bien comptée « dans le LFE », mais son décalage
  ne peut pas être mesuré : il n'y a rien de commun à corréler.
- Une plage de recherche large (défaut ±250 ms) trouve un LFE très en retard,
  mais perd les coups faibles noyés dans une basse et devient ambiguë quand
  les coups se suivent à moins de la plage. Sur une musique dense, la réduire.
- Le retard d'attaque à attaque est grossier (quelques ms, 2 ms de pas) et ne
  dit rien de la polarité.
- Deux attaques semblables à moins de 370 ms l'une de l'autre : la seconde est
  comparée à un passé qui contient la première, et sa mesure perd en précision.
- Sur un grondement grave tenu, une fausse attaque par minute environ
  subsiste. Elles ne donnent un verdict que si le LFE ressemble au mix.
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
17. AES Technical Document TD1009 (recommandations sur le canal LFE et les
    downmix automatiques).
18. ATSC A/52, *Digital Audio Compression (AC-3, E-AC-3)*, traitement du
    downmix deux canaux.
19. EBU Tech 3343, *Guidelines for Production of Programmes in accordance with
    EBU R 128* ; UIT-R BS.1770.
20. Netflix, *Sound Mix Specifications & Best Practices*.
21. S. Fan, E. Kuo, S. Shah, M. Bosi, « Transient Detection Methods for Audio
    Coding », AES 155th Convention, Express Paper 131, 2023.
22. T. Darwen, *AudioLib — Transient Detection*,
    github.com/tmdarwen/AudioLib (Documentation/TransientDetection.md).
23. B. Zackay, E. O. Ofek, A. Gal-Yam, « Proper Image Subtraction — Optimal
    Transient Detection, Photometry, and Hypothesis Testing », *Astrophys. J.*,
    vol. 830, 2016.
24. O. Springer, E. O. Ofek, B. Zackay et al., « Translient: Detecting
    Transients Resulting from Point-source Motion or Astrometric Noise »,
    *Astron. J.*, 2024 (arXiv:2403.09771) ; code : github.com/ofersp/translient.
25. H. Møller, C. S. Pedersen, « Hearing at low and infrasonic frequencies »,
    *Noise & Health*, vol. 6, 2004.
26. RTINGS, méthodes de mesure « Low-Frequency Extension » des téléviseurs et
    barres de son.

## Niveau du LFE et réglages

**Case « LFE +10 dB ».** Elle s'applique partout : spectre, alertes, systèmes
de lecture et score. Cochée (défaut), le LFE compte 10 dB plus fort qu'un canal
d'écran, comme en salle [9]. Décochée, il est pris tel qu'enregistré.

**Panneau « Dev ».** Tout ce qui est un choix et non une mesure y est
réglable : tolérances des trois niveaux, poids d'un sub en retard ou en avance,
plafond de la dépendance au LFE, poids et bornes de l'infra-grave, poids de la
dépendance au niveau dans le score et moyenne à laquelle sa case est rouge.
