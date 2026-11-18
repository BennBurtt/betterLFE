# Algorithme de SubGUARD

Le LCR est toujours la référence : tout est exprimé comme « ce que fait le LFE
par rapport au canal d'écran ». L'analyse porte sur la bande commune aux deux,
de 5 à 190 Hz (affichée de 5 à 150 Hz), après filtrage anti-repliement et
décimation vers ~500 Hz.

Tout est calculé sur des rapports ou en énergie logarithmique : les deux
détecteurs se comportent de la même façon de −10 dBFS à −100 dBFS. En dessous
de −100 dBFS pendant 0,5 s, le LFE est tenu pour absent et ses mesures sont
effacées (point 5) : à 85 dB(C), −100 dBFS donne environ 5 dB SPL, inaudible à
toutes les fréquences de la bande. Le silence numérique est écarté.

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
   `σ = 1/√(2K)`. Les fenêtres se recouvrant de 96 à 99 %, `K` n'est pas un
   nombre de fenêtres mais le produit durée × bande effectives, ce qui tient
   compte de leur dépendance. Vérifié par Monte-Carlo sur des signaux
   indépendants (test « unrelated signals and onsets ») : 0,00 % de fausses
   alertes en son tenu, 0,74 % aux attaques, écart mesuré / σ prévu = 0,93.
4. Alerte quand `|c|` dépasse le seuil choisi **et** `2,5σ`. Trois niveaux, par
   le sélecteur au-dessus du spectre : Soft (0,90), Medium (0,75), Strong
   (0,50, par défaut). En mode « Out of
   phase only », seules les valeurs négatives alertent.
5. Quand le LFE est absent (niveau RMS sous -100 dBFS pendant 0,5 s), tout ce qui
   a été mesuré face à lui est effacé : le calcul repart de zéro au lieu de
   garder la trace d'un son terminé. Un LFE très bas, à −80 dBFS par exemple,
   reste analysé normalement. L'affichage n'est pas effacé pour autant : chaque alerte reste à son
   maximum 1,5 s après sa dernière activité (maintien de crête), puis
   retombe au rythme du réglage « Decay ». Un impact de LFE court suivi d'un
   blanc numérique reste ainsi lisible le temps de lever les yeux (vérifié :
   1 s de LFE puis silence, alerte encore à 1,00 0,9 s après la remise à zéro,
   éteinte 4,5 s après).

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

Les mêmes détecteurs tournent aussi sur **L − R** : une attaque jouée en L et
en R en polarités opposées disparaît du downmix (L + R = 0) mais pas de
L − R ; tout contenu d'écran apparaît dans l'une des deux. Une attaque de
L − R n'est retenue que là où le downmix annule vraiment le grave : énergie
de L − R **sous 200 Hz** (passe-bas Butterworth du 2ᵉ ordre), sur 50 ms, au
moins 6 dB au-dessus de celle du downmix dans la même bande. Seule
l'annulation du grave compte face au LFE, et mesuré en pleine bande, un
centre chargé (dialogues) masquerait l'annulation (vérifié : kick en L et −R
sous un son de 1 kHz fort au centre, 0 attaque trouvée sur 5 en pleine bande,
5 sur 5 sous 200 Hz). L'attaque est alors analysée contre L − R. Sur de la musique ordinaire, rien ne change (mêmes
24 attaques qu'avant) ; un kick en L et R en opposition est trouvé 5 fois sur
5 et le retard de 12 ms du LFE lu exactement (vérifié).

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
   millisecondes n'y a de toute façon guère de sens à l'écoute. L'app n'annonce
   donc jamais une précision meilleure qu'un seizième de période du son commun
   aux deux signaux : 2 ms à 30 Hz, 0,6 ms à 100 Hz.
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

Le bandeau « Transients » affiche le verdict de la dernière attaque du mix,
avec trois degrés de lien entre les deux attaques :

- **même son, confirmé** : ressemblance d'au moins 0,7 et retard connu à 2 ms
  près : verdict fin (retard, polarité, phase) ;
- **son semblable** : ressemblance au-dessus du seuil réglé : verdict fin ;
- **seulement proche dans le temps** : le LFE a une attaque dans la plage de
  recherche mais ne ressemble pas au mix ; ce peut être un autre son tombé
  dans la fenêtre. Le retard est alors chronométré d'attaque à attaque, sans
  lecture de phase, et le verdict est affiché en couleur atténuée ;

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
| Dans le grave, les courbes d'isosonie sont resserrées : de petits écarts de niveau s'entendent beaucoup | ISO 226:2023 [14], Leembruggen & Newell [15] | Les dB sont convertis en sensation, bande par bande |
| Un écart de niveau commence à s'entendre vers 1 dB | psychoacoustique classique [16] | En dessous de 1 dB, aucune pénalité |

### 3.2 Des dB à la sensation (ISO 226:2023)

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

**Niveau d'écoute.** La conversion n'est plus figée à 80 phones : elle suit le
niveau réel du grave et le niveau d'écoute choisi (bouton « Listening » à
gauche de Reset, toujours affiché : 70 à 90 dB(C) ; 76 à 85, en clair, sont les
niveaux des auditoriums de mixage).

1. Le niveau d'écoute est le niveau de pression qu'un canal d'écran produit
   avec le signal de calibrage à −20 dBFS RMS : 85 dB(C) en salle [9].
2. Le grave de référence de chaque bande a un niveau RMS en dBFS ; son niveau
   de pression est `niveau d'écoute + (dBFS + 20)`. Le LFE y entre avec son
   gain de lecture (+10 dB si la case est cochée).
3. ISO 226:2023 [14] donne le niveau d'isosonie `LN` (phones) d'un son pur de
   cette fréquence à cette pression, par ses formules (1) et (2) :
   `Lf = (10/αf)·lg[(4·10⁻¹⁰)^(0,3−αf)·(10^(0,03·LN) − 10^0,072) + 10^(αf·(Tf+LU)/10)] − LU`
   et l'inverse
   `LN = (100/3)·lg[(10^(αf·(Lf+LU)/10) − 10^(αf·(Tf+LU)/10)) / (4·10⁻¹⁰)^(0,3−αf) + 10^0,072]`,
   avec les paramètres `αf`, `LU`, `Tf` de sa table 1 (interpolés en fréquence
   logarithmique entre les tiers d'octave). 0,3 est l'exposant de sonie à
   1 kHz (0,25 dans l'édition 2003), 0,072 = 0,3 × 2,4 dB / 10 le seuil à
   1 kHz. Valable de 20 à 90 phones, indicative en dessous de 20 phones.
   L'édition 2023 diffère de celle de 2003 de 0,6 dB au plus ; son seuil à
   20 Hz est descendu de 78,5 à 78,1 dB, comme ISO 389-7:2019. Vérifié : au
   seuil, un son pur est à 2,4 phones à toutes les fréquences, et la formule
   (2) est bien l'inverse de la (1).
4. Doubler la sensation, c'est monter de 10 phones : le nombre de dB par
   doublement est `Lp(LN + 10) − Lp(LN)`, pris entre 20 et 80 phones (la norme
   couvre 20 à 90).

En pratique ce nombre varie peu : à 25 Hz, de 5,9 dB vers 20 phones à 5,0 dB
à partir de 60 phones ; à 100 Hz, de 8,4 à 6,9 dB. Le niveau d'écoute change
donc peu la conversion ; il change surtout ce qui est audible (ci-dessous).
Limites : la norme vaut pour des sons purs, de face, en champ libre, alors
qu'on juge ici des bandes de bruit en salle ; et la lecture en dB(C) du bruit
de calibrage est prise égale à son niveau de pression.

**Audibilité.** Un grave que personne n'entend ne compte pas ; un grave au
seuil compte pour la part des spectateurs qui l'entendent.

1. Seuil : le seuil d'audition d'ISO 226:2023 [14] (colonne `Tf` : 78,1 dB SPL
   à 20 Hz, 68,7 à 25 Hz, 51,1 à 40 Hz, 37,5 à 63 Hz, 26,5 à 100 Hz). C'est
   le seuil médian de jeunes auditeurs otologiquement normaux, en champ libre
   et binaural ; il est identique à celui d'ISO 389-7 [28], et de 20 à 250 Hz
   le seuil en champ diffus (une salle) est le même qu'en champ libre [27].
2. Point par point, sur un filtre auditif : le seuil varie trop à l'intérieur
   d'une bande de notation pour une seule valeur (il baisse d'environ 22 dB
   entre 31,5 et 63 Hz, dans la seule bande 30–60 Hz). À chaque point de la
   grille d'analyse (80 points de 20 à 120 Hz), on prend le niveau du grave
   sur un filtre auditif centré sur ce point (ERB de Glasberg & Moore [29] :
   24,7·(4,37·f/1000 + 1), soit 27 Hz à 25 Hz, 29 Hz à 40 Hz, 32 Hz à 70 Hz,
   36 Hz à 100 Hz : l'oreille intègre l'énergie sur cette largeur), qu'on
   compare au seuil du son pur à **sa** fréquence. L'audibilité d'une bande est
   la part de sa puissance au-dessus du seuil. Un système de lecture ou une
   place de salle qui monte ou baisse la bande décale ce profil d'autant.
   Vérifié : deux sons purs de même niveau dans la bande 30–60 Hz sont
   entendus par 4 % des auditeurs à 33 Hz et par 86 % à 57 Hz.
3. Dispersion entre auditeurs : les seuils individuels se répartissent autour
   de la valeur de la norme ; ISO 28961 [27] en donne les centiles, et la
   littérature sur les basses fréquences cite un écart-type de l'ordre de
   5 dB [25][30]. La part d'auditeurs qui entendent une bande de niveau `L`
   est `Φ((L − Tf) / 5 dB)`, Φ étant la loi normale : 50 % au seuil, 98 % à
   10 dB au-dessus, 2 % à 10 dB en dessous.
4. Dans le score : chaque bande pèse selon le grave **entendu** (sa puissance
   × la part qui l'entend), et la note d'un système est multipliée par la part
   d'auditeurs qui entendent le grave tout court (celle de sa bande la plus
   audible). Une bande inaudible rendue audible par un boost compte aussi. La
   dépendance au niveau du sub ne retient que les écarts entendus.

Conséquence : le même mix, joué plus bas, ou écouté à un niveau de calibrage
plus faible, perd les problèmes devenus inaudibles. Mesuré : un LFE identique
à la façade vaut 0 à −20 dBFS et 100 à −86 dBFS (inaudible à 85 dB(C)) ; à
−54 dBFS, 27 à 85 dB(C) et 100 à 70 dB(C). La croissance très rapide de la
sonie juste au-dessus du seuil dans le grave [25] justifie qu'une bande
nettement au-dessus du seuil compte entièrement.

Limites : le masquage par le reste du mix n'est pas modélisé, seul le seuil
absolu l'est ; et le seuil vaut pour de jeunes auditeurs, il est plus haut chez
la plupart des adultes. L'audibilité se désactive dans le code
(`tuning.audibility`) si besoin.

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
| Bass management | canaux d'écran croisés vers le sub (Linkwitz-Riley 4ᵉ ordre) à 60, 80, 100 Hz, LFE passe-bas 120 Hz ; avec et sans passe-bas raide à 120 Hz tenant lieu de filtre d'encodeur. Le grave d'écran redirigé vers le sub garde son niveau (gain 1, recombiné en passe-tout) ; seul le canal LFE reçoit le +10 dB, comme le distingue la SMPTE RP 200 [9] | 6 |
| Stéréo | le mix sans son LFE, comme dans tout downmix deux canaux. Seul le LFE est retiré : la façade reste sommée comme dans la référence, pour que la façon dont L, C et R se replient (qui ne dépend pas du LFE) ne compte pas comme une perte | 1 |

**Dépendance au niveau du sub (« LFE level dependence »).** Un avertissement,
qui retire aussi jusqu'à 20 % du score. Chacun des 16 systèmes cinéma et bass management est recalculé avec son
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
   nombre de dB qui double la sonie dans cette bande, calculé par ISO 226 au
   niveau réel de la bande au niveau d'écoute choisi (voir 3.2 ; vers 80 phones
   il vaut environ 5,0 ; 5,6 ; 6,4 ; 7,0 dans les quatre bandes), et
   `T` la tolérance du niveau choisi (voir 3.6). Zéro jusqu'à `T`, 1 quand la
   bande est entendue deux fois plus forte ou deux fois plus faible.
3. `pénalité du système = Σ part de la bande × pénalité de bande`, la part étant
   celle de la bande dans le grave de la référence. Juger par bande évite
   qu'une annulation à 40 Hz soit masquée par du niveau à 100 Hz.

### 3.6 Score

Deux risques distincts, multipliés, plus une légère retenue pour la dépendance au niveau du sub (20 % au plus, voir 3.4) :

`robustesse = 100 × (1 − interaction) × (1 − dépendance) × (1 − 0,20 × dépendance au niveau)`

**Interaction** : la plus forte valeur de `pertinence × pénalité` parmi les
systèmes cinéma et bass management. C'est le pire cas, pas une moyenne : un mix
qui s'annule sur un système plausible n'est pas rendu sûr par les systèmes où
il survit.

| Système | Pertinence |
|---|---|
| Cinéma, sub à 0 ms (les deux polarités) | 1 |
| Cinéma, sub en retard (+2,5 et +5 ms) | 0,5 |
| Cinéma, sub en avance (−5 et −2,5 ms) | 0,5 |
| Bass management (les six cas) | 1 |

**Dépendance** : `1,00 × pénalité de la stéréo sans LFE`, c'est-à-dire de ce que
le grave perd partout où le LFE manque ou est mal réglé. Le plafond de 1,00
place un grave large bande entièrement dans le LFE vers 22 : grave (la salle
seule en décide) sans valoir une annulation.

**L'infra-grave est mis à part.** Ce que la stéréo perd est pondéré par la
fréquence : 30 % en dessous de 25 Hz, tout à partir de 40 Hz, et entre les
deux un poids qui monte linéairement en octaves. Une sinusoïde de 20 Hz seule
dans le LFE coûte donc peu : on ne la ressent qu'en salle, et l'enlever change
peu le downmix stéréo. Elle garde 30 % de son poids, pour ne pas compter
pour rien.

| Grave présent uniquement dans le LFE | Dépendance | Score |
|---|---|---|
| Sinusoïde à 20 Hz | 0,30 | 70 |
| 30 Hz | 0,59 | 41 |
| 40 Hz | 0,96 | 4 |
| 50 Hz | 1,00 | 0 |
| 70 Hz et plus | 1,00 | 0 |
| Bruit large bande 20–120 Hz | — | 2 |

Les bornes viennent de trois constats (voir 3.8 bis) : le seuil d'audition
monte très vite vers le bas (78,1 dB SPL à 20 Hz, 68,7 dB à 25 Hz, 37,5 dB à
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

Les repères ne dépendent pas du niveau de sévérité : LFE identique à la façade 0, LFE seul
vers 2 avec les réglages par défaut (plafond 1,00 ; un plafond de 0,80 environ le remet vers 20).

### 3.7 Lecture du score et valeurs de référence

| Score | Sens |
|---|---|
| 100 | Aucun système ne déplace le grave de façon audible, et il ne repose pas sur le LFE |
| 50 | Dangereux mais passable : le pire système à l'heure déplace le grave à mi-chemin d'un doublement ou d'une division par deux de la sensation (environ 3,5 dB) |
| 20–25 | Tout le grave est dans le LFE : la salle seule en décide |
| 0 | Un système plausible annule le grave, ou le rend deux fois plus fort |

Vérifiées par les tests, sur bruit blanc à −20 dBFS, au niveau Strong, écoute à 85 dB(C) avec audibilité (dans ce bruit, la bande 20–30 Hz est sous le seuil et ne compte pas) :

| Mix | Robustesse |
|---|---|
| Façade seule, LFE muet | 100 |
| LFE sans rapport avec la façade, 10 dB sous elle à l'écoute | 100 |
| LFE 10 dB sous la façade, à 30 % copié sur elle | 94 |
| LFE sans rapport, aussi fort que la façade | 56 |
| LFE 10 dB sous la façade, à 60 % copié sur elle | 60 |
| LFE seul | 2 |
| LFE sans rapport, 6 dB au-dessus de la façade | 2 |
| LFE aussi fort que la façade, à 60 % copié sur elle | 19 |
| LFE 10 dB sous la façade, entièrement copié sur elle | 20 |
| LFE identique à la façade | 0 |
| LFE identique mais en retard de 5 ms | 0 |

Même 10 dB sous la façade, un LFE entièrement copié sur elle fait varier le
grave de près de 6 dB entre un sub en polarité et un sub inversé.

### 3.8 Ce qui est établi et ce qui est supposé

| Élément | Statut |
|---|---|
| LFE à +10 dB, omission du LFE en downmix, rôle d'appoint du LFE | Normes [9][12][13] |
| Conversion dB → sensation (5,0 à 7,0 dB par doublement vers 80 phones) | Calculé depuis ISO 226:2023 [14] |
| Seuil d'audition | ISO 226:2023 [14] / ISO 389-7:2019 [28], point par point sur la grille, niveau pris sur un filtre auditif [29] |
| Dispersion des seuils (écart-type 5 dB) | Ordre de grandeur publié [25][30] ; les niveaux d'isosonie mesurés qui fondent ISO 226:2023 ont eux-mêmes un écart-type d'environ 5 à 6 dB [37]. ISO 28961 [27] donne la distribution exacte des seuils, fréquence par fréquence : non consultée (payante), la valeur uniforme de 5 dB reste une approximation |
| Tolérances des trois niveaux (3 ; 2 ; 1 dB) | Choix ; 1 dB est l'ordre de grandeur admis du plus petit écart audible [16] |
| Filtres de bass management (Linkwitz-Riley 24 dB/oct) | Modèle courant, variable selon les appareils |
| Filtre d'encodeur (Butterworth 8ᵉ ordre à 120 Hz) | Modèle : le filtre LFE de Dolby Digital Plus n'est pas public |
| Étendue des défauts de salle (±5 ms, polarité) | **Hypothèse.** Le rapport SMPTE constate de larges écarts mais ses chiffres ne sont pas publics |
| Pertinences des systèmes, plafond de dépendance (1,00), bornes et poids de l'infra-grave (25 et 40 Hz, 30 %), poids de la dépendance au niveau (20 %) | **Hypothèse** d'ingénierie, suivant la consigne (0 ms ×2, avance ×0,5 ; LFE seul vers 20–25). Aucune statistique publiée sur la fréquence de ces défauts |
| Acoustique de la salle (modes, places) | Hors des systèmes électriques ; simulée à part par Room Sim et Room Sim Pro (modèle modal) |
| ISO 226 pour des bandes de bruit et de la musique | **Approximation** : ISO 226 décrit des sons purs continus. Un modèle de sonie pour sons complexes (ISO 532-3, Moore-Glasberg-Schlittenlacher) serait plus juste, au prix d'un calcul bien plus lourd |
| Le score sur 100 | **Indice de risque propre à SubGUARD**, construit à partir de mesures et de normes mais avec des pondérations choisies ; ce n'est pas une mesure normalisée de la qualité d'un mix |

### 3.8 bis Dépendance au LFE : ce que dit la littérature

| Source | Ce qu'elle établit |
|---|---|
| UIT-R BS.775 [12] | Le LFE est optionnel à la réception et ne doit porter que de l'information d'appoint, pas tout le grave du programme |
| AES TD1009 [17] | Le LFE est en général omis des downmix automatiques ; éviter d'y placer seul un contenu essentiel |
| ATSC A/52 (AC-3, E-AC-3) [18], Dolby [13] | Le downmix deux canaux réunit tous les canaux sauf le LFE |
| EBU Tech 3343 / UIT-R BS.1770 [19] | Le LFE est exclu de la mesure de sonie : un grave placé dans le LFE échappe au contrôle de loudness |
| Netflix, spécifications de mixage [20] | Un stéréo fabriqué à la main peut inclure le LFE, replié entre −8 et −12 dB et filtré à 200 Hz ou moins |

| ISO 226:2023 [14] | Seuil d'audition : 78,1 dB SPL à 20 Hz, 68,7 dB à 25 Hz, 37,5 dB à 63 Hz ; applicable aux bandes de bruit d'un tiers d'octave ou plus étroites (note 1) |
| Møller & Pedersen 2004 [25] | Sous 20 Hz environ, le son perd son caractère tonal ; le ressenti corporel (vibration) apparaît 20 à 25 dB au-dessus du seuil d'audition |
| Mesures de téléviseurs et de petites enceintes [26] | Extension dans le grave : 60 Hz est déjà une bonne valeur pour un téléviseur ; les petits haut-parleurs coupent vers 150 Hz |

Les premières vont dans le même sens : ce qui n'existe que dans le LFE peut manquer.
Mesurer la dépendance par ce que perd la stéréo sans LFE est donc la traduction
directe de ces textes. Aucune ne donne de seuil chiffré (« à partir de quelle
part le LFE porte trop »), ni de statistique sur la proportion de spectateurs
écoutant sans caisson : le plafond de 1,00 reste un choix.

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

## 3 bis. Room Sim : quinze salles, place par place

Onglet « Room Sim » (il remplace l'analyseur ; le score et la frise restent),
et salles cochées dans le score.

**Dans le score.** Les salles cochées (dans Playback chains ou en cliquant une
ligne de Room Sim) comptent comme des systèmes de lecture de plus : une salle
pèse la pénalité de sa place moyenne (1 − score de la salle / 100), et le score
garde le pire cas, comme pour les autres systèmes. Par défaut, cinq salles du
milieu de gamme sont cochées : multiplexes de 150, 200 et 300 places, grande
salle de 400 places, grand format premium.

**Modèle.** Chaque salle est une boîte rectangulaire aux parois rigides,
uniformément amorties. La pression en une place r due à une source r0 est la
somme des modes propres [31] :

`H(r, r0, ω) = Σ ψn(r)·ψn(r0) / (Λn·(ωn² − ω² + 2j·δ·ω))`

avec `ψn = cos(nx·π·x/L)·cos(ny·π·y/W)·cos(nz·π·z/H)`, les fréquences propres de
Rayleigh `fn = (c/2)·√((nx/L)² + (ny/W)² + (nz/H)²)`, `Λn` le produit de 1
(indice nul) ou 1/2, et `δ = 6,91/TR60`. Les modes jusqu'à 200 Hz sont sommés
pour la bande 20–120 Hz. L'écran : trois enceintes derrière la toile, chacune
jouant son propre canal (L, C, R) ; le LFE : deux caissons au pied du mur
avant ; vingt places (5 × 4) dans la zone des sièges, à 1,2 m. Vérifié : le
premier mode axial d'une salle de 10 m tombe bien à c/2L = 17,15 Hz, le
deuxième à 34,3 Hz. Calcul des quinze salles : 0,5 s une fois, puis moins de
1 ms par mise à jour.

**Ce qui est mesuré.** La couleur propre de la salle touche n'importe quel mix
et n'est pas comptée. On garde ce que l'interaction LFE / façade ajoute à
chaque place, par rapport au mix approuvé :

`écart (dB) = 10·log10 [ (P place / P place en puissance) / (P approuvé / P approuvé en puissance) ]`

avec `P place = Σ_ab H_a·conj(H_b)·S_ab` sur L, C, R et LFE (S : matrice
interspectrale du mix, H : chemin de chaque canal jusqu'à la place),
`P place en puissance` la même somme sans les termes croisés entre le LFE et
l'écran (les croisés L / C / R restent), et `P approuvé` / `P approuvé en
puissance` la même chose en régie (chemins plats). Les termes croisés LFE /
écran sont donc bien comptés, à la place et en régie : l'écart dit ce que la
salle change à leur effet.

Un LFE décorrélé donne 0 dB à toutes les places de toutes les salles (vérifié).

**Étalonnage et niveau d'écoute.** Chaque salle est étalonnée comme une vraie :
l'écran et les caissons sont réglés chacun pour que leur niveau moyen sur les
places (20–120 Hz) égale celui de la régie, comme un étalonnage à plusieurs
micros. Chaque place est alors au-dessus ou au-dessous de ce niveau selon ce
que la salle y fait. L'audibilité (seuil ISO 226, dispersion 5 dB) et le
nombre de dB qui doublent la sonie sont pris au niveau réel du grave à cette
place, bosses et creux de la salle compris. L'acoustique de la salle ne dépend
pas du niveau (elle est linéaire) ; ce qui en dépend, c'est ce qu'on en
entend : plus le niveau d'écoute est élevé, plus ses défauts sont audibles.
Mesuré, LFE identique à la façade à −50 dBFS : note moyenne des salles 99 à
70 dB(C), 58 à 90 dB(C) (score de salle pesant les pires places).

Chaque place est notée comme un système de lecture (tolérance, doublement de
sonie ISO 226, audibilité) : note de la place = 100 × (1 − pénalité).
**Score de la salle** : la note moyenne des places, tirée vers le bas par les
pires places,

`score = (1 − w) × moyenne des places + w × moyenne des pires places`

avec par défaut w = 0,25 et les pires places = 10 % des places (au moins une),
réglables dans Dev. Ainsi 17 bonnes places et 3 mauvaises (30 %) donnent 74,6
au lieu de 89,5 en moyenne simple (vérifié) : quelques places ratées coûtent
10 à 30 points à une bonne salle. w = 0 redonne la moyenne simple.
**Résultat = robustesse du grave × score de la salle / 100**.

**Les quinze salles** sont des exemples représentatifs (dimensions et TR60
typiques), pas des salles mesurées : du couloir vif (18 × 6 × 5 m, 0,9 s) à
l'IMAX traitée (30 × 22 × 16 m, 0,4 s).

**Ce que le modèle apprend.** Les caissons et l'écran étant sur le même mur,
leur écart de phase varie peu d'une place à l'autre. Avec un LFE identique à la
façade et aussi fort, la plupart des places perdent 1 à 4 dB par rapport au
gain « en phase » entendu en régie, avec 1 à 2 dB d'écart entre places. Un
premier modèle statistique (phase au hasard d'une place à l'autre) annonçait
16 dB d'écart : trop pessimiste pour des caissons sous l'écran, il a été retiré
au profit de cette simulation.

**Limites.** Les salles réelles ne sont pas des boîtes (sol en gradins, murs
évasés, absorption variable avec la fréquence, caissons multiples réglés en
délai par l'installateur).

## 3 ter. Room Sim Pro : trois salles à soi

Onglet « Room Sim Pro » (exclusif avec Room Sim). Le but : sortir de
l'implantation cinéma standard et tester le mix dans des salles construites à
la main (exposition, défilé, galerie, tente, église, salon, régie), enceintes
et rappels placés où l'on veut. **Son score n'entre pas dans le score global** ;
il a le sien.

**Chaque canal joue depuis ses propres enceintes.** L'analyse fournit la
matrice interspectrale complète de L, C, R et du LFE (puissances et
interspectres complexes de chaque paire). À chaque place, la salle est une
matrice 4 × 4 de produits de chemins `H_a · conj(H_b)` (a, b ∈ {L, C, R, LFE}),
moyennée sur la bande de chaque point ; la puissance entendue est
`Σ_ab P_ab · S_ab`, où S est la matrice du mix. Ainsi, du grave seulement dans L
n'est entendu que là où porte l'enceinte L : avec un LFE identique à L et un
caisson à droite, les places de gauche bougent de 4,7 dB, celles de droite de
0,8 dB (vérifié). Room Sim joue aussi chaque canal d'écran par sa propre
enceinte. En Pro, la référence est le mix d'écran tel quel ; le grave du LFE
y est attendu là où est celui de l'écran, dans les proportions L / C / R du
mix. Étalonnage : chaque groupe d'enceintes (L, C, R, caissons) est réglé
séparément au niveau de la régie ; les rappels gardent leur niveau face à
l'enceinte d'écran de leur côté. C'est l'équivalent d'un réglage au bruit
rose et au sonomètre : la même **énergie totale** de 20 à 120 Hz sur les
places d'étalonnage, son direct et réverbéré ensemble (sous la fréquence de
Schroeder, ils ne se séparent pas : ce sont les mêmes modes). Une salle vive
reçoit donc des gains plus bas qu'une salle mate ; c'est voulu, c'est ce que
ferait un installateur, mais il faut s'en souvenir en comparant deux salles
aux TR60 très différents.

**Divergence (comme le panner de Pro Tools).** Une matrice de routage répartit
les canaux sur les groupes d'enceintes, en puissance : *Centre %* (part de C
jouée par l'enceinte centrale, le reste en centre fantôme dans L et R, −3 dB
chacun à 0 %) ; *Front* (100 : chaque canal sur son enceinte ; 0 : chaque canal
réparti également sur L, C et R) ; *Rear* (100 : les rappels gauches jouent
Lc, les droits Rc ; plus bas, les deux côtés partagent leurs signaux) ;
*F / R* (plus bas, une part des canaux d'écran part vers les rappels). Les
rappels jouent Lc / Rc = L ou R + C à −3 dB, ou le LFE. Vérifié : routage
unitaire à 100, −3 dB en L et R à Centre 0 %, 1/√3 partout à Front 0.

**Même note que Room Sim, une référence plus exigeante.** Même modèle modal
(modes jusqu'à 160 Hz), même note place par place et même score de salle
(`RoomSimulator::rate`), mêmes réglages Dev, même niveau d'écoute, même
Soft / Medium / Strong, même LFE +10 dB. Une différence voulue :

- Room Sim ne compte que l'interaction entre l'écran et le LFE ; la couleur
  que chaque chemin prend dans la salle est retirée.
- **Room Sim Pro prend le chemin de l'écran (L C R) comme référence** :
  `référence = Σ_ab∈{L,C,R} H_a·conj(H_b)·S_ab + Q·S_LFE` à chaque place, où
  `Q` est la réponse moyenne des enceintes d'écran pondérée par les
  puissances L / C / R du mix : comme si le grave du LFE passait par L C R.
  L'écart est ensuite rapporté à la régie comme ci-dessus,
  `(P place / référence) / (P approuvé / P approuvé en puissance)`, ce qui
  revient à `P place / (référence × P approuvé / P approuvé en puissance)` :
  le terme croisé LFE / écran du mix validé est dans la comparaison. La couleur propre du chemin d'écran est
  retirée, mais le chemin LFE entendu autrement que lui compte : caissons
  placés ailleurs, réglés hors niveau, dans un autre mode, en proportion du
  grave que le mix confie au LFE. Un mix sans LFE ne change à aucune place
  (vérifié) ; un LFE décorrélé et aussi fort que l'écran passe de 74,6 (caissons
  sous l'écran, 4,8 dB d'écart entre places) à 32,6 avec un seul caisson dans
  un coin du fond (19 dB d'écart) (vérifié). C'est la philosophie de SubGUARD :
  le grave ne doit pas reposer sur le LFE.

Vérifié : avec la référence de Room Sim, une salle Pro construite comme le
multiplex 200 places donne la même note que Room Sim (55,5 contre 55,4).

**Échantillonnage fin.** Chaque point de la grille d'analyse représente une
bande d'environ 2,3 %. Quand les modes sont plus étroits que cette bande
(décroissance longue : largeur ≈ δ/π, 0,2 Hz à 10 s), la réponse est moyennée
sur 2 à 8 fréquences dans la bande au lieu d'être lue en son centre ; les
puissances et le produit croisé des deux chemins sont moyennés, pas les
réponses complexes.

**Ce qu'on règle, par salle** (panneaux de potentiomètres, à la manière du
panner de Pro Tools ; la molette fait défiler la colonne, un clic sur la
valeur permet de la taper) :

- **Room** : nom ; point de départ (les quinze salles de Room Sim, ou sept
  lieux hors cinéma) ; forme rectangulaire ou ronde ; longueur, largeur
  (ou diamètre), hauteur.
- **Acoustics** : décroissance réglée à la main (TR60 de 0,1 à 10 s) ou
  calculée d'après les matériaux des six surfaces (voir plus bas). Affichés :
  TR60 d'Eyring à 63 et 125 Hz, fréquence de Schroeder.
- **L C R** : un jeu, ou deux jeux dos à dos (le second est le premier
  tourné de 180° autour du centre de la salle : on place le premier, le
  second suit). L et R liés par défaut (« Link ») : R est le miroir de L par
  rapport à l'axe de la salle ; délié, chacun a son panneau. Puis Centre. Pour
  chaque enceinte : distance au mur avant (front wall, celui de l'écran),
  distance au mur gauche, hauteur, délai, niveau, polarité (Ø), marche / arrêt
  (On). Au grave, une enceinte rayonne de la même façon dans toutes les
  directions : son orientation ne compte pas, seule sa position compte.
- **Subwoofers** : 1 à 4, mêmes réglages.
- **Fill speakers** (rappels) : On, Link (les rappels 2 et 4 sont les miroirs
  de 1 et 3 : on règle des paires), 1 ou 2 paires (ou 1 à 4 déliés), mêmes
  réglages plus ce qu'ils jouent : **Lc / Rc** (L ou R + C à −3 dB) ou LFE.
  Un rappel gauche reçoit L avec un gain de 1 et C avec un gain de 0,708 ; la
  corrélation réelle entre L et C (qu'ils s'additionnent en amplitude ou en
  puissance) vient de la matrice interspectrale du mix. Délai par défaut : aligné sur l'écran à
  leur propre distance (x / c).
- **Audience** : trois dispositions. En rangs (premier et dernier rang, part
  de la largeur, rangs, places par rang) ; en deux blocs avec une allée libre
  au milieu (podium de défilé, scène : part de la largeur laissée libre) ; en
  anneaux autour du centre (rayon intérieur et extérieur, nombre d'anneaux,
  places sur l'anneau extérieur, les anneaux intérieurs en ont moins). Hauteur
  d'oreille, gradins. 96 places au plus.
- **Calibration** : moyenne de toutes les places, ou place de référence (aux
  deux tiers, au centre).

**Salle ronde.** Un cylindre à parois rigides, modes exacts (Morse & Ingard
[35]) : `ψ = Jm(kr·r) · cos ou sin(mθ) · cos(nz·π·z/H)`, `kr = j'mn / a`, où
j'mn est le n-ième zéro de la dérivée de la fonction de Bessel Jm et a le
rayon. Jm est calculée par la règle des trapèzes sur sa représentation
intégrale (convergence exponentielle sur un intégrande périodique [36]) et
les zéros par encadrement. Vérifié : J0 s'annule à 2,4048, les zéros de J'm
valent 1,8412 (m = 1), 3,0542 (m = 2), 3,8317 (m = 0), et le premier mode d'une
salle ronde de 10 m de diamètre tombe à c × 1,8412 / (2π × 5) = 20,10 Hz.
Décroissance de chaque mode par les surfaces qu'il touche, comme pour la
boîte, la paroi pesée par la part du mode qui la longe :
`δ = c / (16 V) · Σ (moyenne de ψ² sur la surface / moyenne dans le volume) · S · α'`
(rapport 1 / (1 − m²/j'²) pour la paroi, plafonné à 4 ; 2 ou 1 pour le sol et
le plafond).

**Sols ovale, à coins arrondis, en L : calcul numérique.** La hauteur reste
séparable (sol et plafond plats) ; les modes du plan du sol sont trouvés par
Rayleigh-Ritz : les modes de Neumann (parois rigides) rendent stationnaire
`∫ ∇u·∇v = k² ∫ u v` sur le sol, sans condition à imposer à la paroi
(naturelle). Fonctions d'essai : les modes en cosinus du rectangle englobant
(jusqu'à 1,5 fois le nombre d'onde gardé, 700 au plus) ; intégrales exactes
en travers (chaque colonne du sol est un intervalle), règle du point milieu
en long ; puis `K c = k² M c`, résolu par Cholesky et réduction de Householder
+ QL implicite (EISPACK tred2 / tql2). Chaque mode trouvé est normalisé sur
le sol lui-même (∫ u² = 1 avec la matrice M régularisée) ; sa norme réelle,
calculée avec M sans régularisation, vaut alors environ 1 pour un mode
physique et environ 0 pour un mode parasite porté par la régularisation (une
combinaison de fonctions presque nulles sur le sol) : ceux dont elle est sous
0,5 sont écartés. Ce seuil ne dépend pas du rapport des surfaces du sol et
du rectangle englobant. Vérifié : le nombre de modes trouvés suit la loi de
Weyl pour un problème de Neumann, `N(k) = A·k²/4π + P·k/4π`, à 3,8 % près
pour les trois formes et trois proportions (1, 1,6 et 3) ; aucun mode ne
manque. Vérifié : un « ovale »
de 10 × 10 m (un cercle) donne son premier mode à 20,10 Hz, la valeur exacte
de Bessel. Formes : ovale inscrit, coins arrondis (rayon = 1/4 du petit côté),
L (le quart arrière droit retiré). Temps : 0,6 à 0,8 s pour 20 × 13 m. La
décroissance de chaque mode suit la part du mode le long de la paroi, comme
pour la salle ronde.

**On règle d'abord la salle, puis on place les enceintes.** Les positions sont
gardées en proportion de la salle (le rang de sièges aussi) et affichées en
mètres. Agrandir la salle déplace les enceintes avec elle : une enceinte à 1 m
du mur gauche d'une salle de 10 m de large est à 2 m dans une salle de 20 m,
toujours au même endroit relatif (vérifié).

**Absorption par les matériaux.** Chaque surface (mur avant, mur arrière, murs
gauche et droit, sol, plafond) reçoit un matériau réel, avec son coefficient
d'absorption α à 63 et 125 Hz (interpolé en log de fréquence entre les deux,
constant au-delà). Chaque mode décroît alors selon les surfaces qu'il frappe
(Kuttruff [31], faible admittance de paroi, α = 8·Re β) :

`δn = c / (8 V) · Σ surfaces  w · S · α(fn)`

avec `w = 1` si le mode a un ordre non nul perpendiculaire à cette surface (il
la frappe) et `w = 1/2` s'il court le long. Un mode oblique retrouve la
décroissance d'Eyring ; un mode axial ne perd son énergie que sur ses deux murs
et sonne plus longtemps (vérifié : salle de 20 × 13 × 7 m tout en plaque de
plâtre, TR60 d'Eyring 0,87 s, mode oblique 0,87 s, mode axial 1,47 s). Pour une
forte absorption, α est remplacé par −ln(1 − α) (Eyring [32]), plafonné à
α = 0,95 : le modèle reste approché pour les surfaces très absorbantes et les
côtés ouverts. TR60 affiché : Eyring, `T = 24 ln10 · V / (c · S · (−ln(1 − ᾱ)))`.
Fréquence de Schroeder `2000 · √(T / V)` [31] : en dessous, la salle se résume
à quelques modes isolés, c'est là que le modèle est le plus juste ; au-dessus,
les modes se recouvrent et le résultat est une moyenne.

| Matériau | α 63 Hz | α 125 Hz | Source |
|---|---|---|---|
| Béton brut ou peint | 0,01 | 0,01 | [33] |
| Brique non vernie | 0,03 | 0,03 | [33] |
| Enduit sur brique ou béton | 0,013 | 0,013 | [33] |
| Marbre, pierre, carrelage | 0,01 | 0,01 | [33] |
| Parpaing peint | 0,10 | 0,10 | [33] |
| Parpaing brut | 0,18 | 0,36 | [33], 63 Hz divisé par 2 |
| Plaque de plâtre sur ossature | 0,29 | 0,29 | [33], 63 Hz gardé |
| Lambris bois sur tasseaux | 0,28 | 0,28 | [33], 63 Hz gardé |
| Vitrage épais, grandes baies | 0,18 | 0,18 | [33], 63 Hz gardé |
| Vitrage ordinaire | 0,35 | 0,35 | [33], 63 Hz gardé |
| Parquet sur solives | 0,15 | 0,15 | [33], 63 Hz gardé |
| Lino, vinyle, caoutchouc sur béton | 0,02 | 0,02 | [33] |
| Moquette sur béton | 0,02 | 0,02 | [33] |
| Moquette sur sous-couche mousse | 0,04 | 0,08 | [33], 63 Hz divisé par 2 |
| Rideau de velours lourd, drapé | 0,07 | 0,14 | [33], 63 Hz divisé par 2 |
| Dalles de plafond acoustique suspendues | 0,25 | 0,50 | estimation (0,3 à 0,7 à 125 Hz) |
| Laine minérale 100 mm au mur | 0,15 | 0,35 | estimation (données types) |
| Pièges à basses (membranes accordées) | 0,60 | 0,70 | estimation (données types) |
| Bardage ou toit en tôle d'acier | 0,20 | 0,15 | estimation (loi de masse) |
| Toile de tente (membrane PVC) | 0,80 | 0,70 | estimation (loi de masse) |
| Côté ouvert (pas de mur) | 1,00 | 1,00 | approximation : rien ne revient, mais le modèle garde des modes de boîte rigide et plafonne α à 0,95 ; une ouverture rayonne en réalité avec une impédance qui dépend de la fréquence |

Ce qui est établi et ce qui est supposé : les valeurs à 125 Hz viennent des
tables publiées [33, 34] ; à 63 Hz elles sont rarement publiées, d'où une règle
explicite (surfaces rigides et panneaux : valeur de 125 Hz gardée ; matériaux
poreux : divisée par 2, car leur absorption chute avec la fréquence). Les
lignes marquées « estimation » n'ont pas de table derrière elles : ce sont des
ordres de grandeur. Les membranes légères (toile, tôle) laissent passer le
grave : vu de l'intérieur, ce qui traverse est perdu, d'où un α élevé tiré de
la loi de masse. L'idée : avec une photo et les dimensions au mètre près, on
choisit les matériaux et on obtient une salle plausible ; si l'on connaît le
TR60 mesuré, le régler à la main reste plus juste.

**Matériaux par famille et par surface.** La liste est rangée par famille
(maçonnerie et pierre, plaques et panneaux, verre, revêtements de sol,
tissus, traitement acoustique, structures légères, ouvertures), par ordre
alphabétique dans la langue affichée, et chaque surface ne propose que ce qui
peut y aller : pas de côté ouvert au sol ni au plafond, pas de moquette au
mur. **Le public n'est plus un matériau** : on choisit des personnes assises
(0,30 / 0,60 par m², Beranek [34], 63 Hz divisé par 2) ou debout (0,10 / 0,20,
estimation) ; leur absorption s'ajoute à celle du sol sur la surface qu'occupe
le public (vérifié : petite galerie 0,70 s vide, 0,53 s avec un public assis).

**Presets propres à Room Sim Pro** (matériaux aux bons endroits) :
expositions (petite galerie, white cube, salle de musée en pierre, hall
d'exposition, hall de centre des congrès, rotonde ovale, galerie en L),
défilés (showroom, halle industrielle, hangar, tente, salle de bal de palace :
public de part et d'autre du podium), boutiques (petite boutique, magasin de
mode, flagship store, concept store à coins arrondis, atrium de centre
commercial), autres lieux (tente, église, salon, régie, salle ronde avec deux
L C R dos à dos). TR60 d'Eyring à 63 Hz de 0,3 s (tentes) à 6 s (rotonde en
plâtre et pierre).

**Sept lieux hors cinéma** (point de départ, matériaux compris ; TR60
d'Eyring à 63 Hz) : galerie « white cube » 20 × 12 × 5 m (1,13 s), halle
industrielle pour défilé 40 × 20 × 10 m, public de part et d'autre du podium
(2,56 s), tente 30 × 15 × 6 m (0,33 s), église en pierre 40 × 14 × 15 m
(3,42 s), salon 6 × 4,5 × 2,6 m (0,49 s), régie traitée 7 × 5,5 × 3 m
(0,51 s), salle ronde de 24 m avec deux L C R dos à dos au centre et le public
en anneaux (1,68 s).

**Ce que change la position d'une enceinte, et ce que change un matériau.**
La position d'une source fixe sa part dans chaque mode (ψ à la source) : dans
un coin, elle excite tous les modes au maximum ; au milieu d'un mur, elle
n'excite pas les modes qui y ont un nœud. Les matériaux fixent la
décroissance de chaque mode, d'après toutes les surfaces que ce mode touche :
une enceinte dans le coin d'un mur en béton et d'un mur peint excite les mêmes
modes que dans le coin opposé, mais chaque mode décroît selon l'ensemble des
parois, pas selon celle qui est derrière l'enceinte. Dans ce modèle (faible
absorption), les matériaux ne déforment pas les modes ; avec des parois très
absorbantes, ils le feraient, ce qui n'est pas modélisé.

**Points de sonde, percentile 10, pire point.** Vingt ou 96 places peuvent
passer à côté d'un creux étroit entre deux sièges. Après le calcul des places,
on repère les quatre places où la salle joue le chemin LFE le plus
différemment du chemin d'écran (écart moyen de niveau entre les deux chemins,
indépendant du mix : trouvé une fois), et on calcule huit points à 0,6 m
autour de chacune, 32 points de plus. Ils sont notés comme les places mais
n'entrent pas dans le score de la salle ; chaque carte affiche le percentile
10 des places (la note sous laquelle tombent 10 % des places) et le pire point
trouvé, sièges et points de sonde compris. Quand ce pire point est entre les
sièges, la vue 3D le marque en magenta (vérifié : 32 points de plus pour une
salle de 20 places, pire point ≤ pire place ≤ P10). Aux fréquences du grave,
un creux fait de 1 à 4 m de large : 0,6 m d'écart suffit à le voir.

**Vue en couches.** À droite de la vue 3D, même taille et même caméra (tourner
ou zoomer l'une fait tourner l'autre), trois couches du grave empilées dans la
salle, chacune un maillage sur une grille régulière du sol à hauteur
d'oreille (12 points sur le grand côté) : un boost soulève la couche, un creux
l'abaisse (12 dB la déplacent de 11 % de la hauteur), sa couleur va du crème
(rien) au rouge (6 dB et plus). La place survolée dans la vue 3D est marquée
par un trait vertical et ses trois valeurs. Les couches :

| Couche | Bande | Pourquoi ces limites |
|---|---|---|
| Infra | sous 20 Hz (calculée dès 10 Hz) | 20 Hz : limite de l'infrason (ISO 7196) et limite basse des courbes d'isosonie d'ISO 226 ; ressenti plus qu'entendu |
| Très grave | 20–60 Hz | 60 Hz : pas de norme ; convention courante entre « sub-bass » et « bass », et limite de nos bandes de notation |
| Grave | 60–120 Hz | 120 Hz : limite haute du canal LFE (Dolby, ATSC A/52) |

**L'infra dans le score de Room Sim Pro.** Sous 20 Hz, la bande 10–20 Hz
est notée comme les autres, sur la base de Møller & Pedersen (2004) [38],
la revue de référence sur l'audition aux basses fréquences et aux
infrasons :

- **seuil** : leur seuil normal proposé sous 20 Hz, une droite de 97 dB à
  10 Hz montant de 11,9 dB par octave vers le bas (régression sur les études
  publiées, leur figure 10), qui correspond à peu près à 97 dB(G) au sens
  d'ISO 7196 ; il est pris à la fréquence où se trouve l'infra du mix. Il ne
  se raccorde pas exactement à ISO 226 à 20 Hz (environ 85 dB contre 78) :
  les auteurs le signalent, aucune solution définitive n'existe autour de
  20 Hz ;
- **dispersion** : 5 dB d'écart-type entre personnes, comme au-dessus de
  20 Hz (même article) ;
- **sensation** : au-dessus du seuil, l'infra devient fort très vite, les
  courbes d'isosonie y sont très resserrées ; mais leurs courbes proposées
  sous 20 Hz sont « à prendre avec une grande réserve ». Les dB qui doublent
  la sonie sont donc pris à 20 Hz dans ISO 226:2023, la plus basse fréquence
  normalisée : l'infra étant plus resserré encore, un écart d'infra compte au
  moins autant qu'à 20 Hz, jamais moins. Selon l'article, on sent des
  vibrations dans le corps dès 20 à 25 dB au-dessus du seuil.

Le reste de la notation est le même (tolérance, poids selon le grave entendu).
Room Sim, lui, reste sur 20–120 Hz.

**Couverture (Room Sim Pro).** Le modèle modal contient la perte avec la
distance, les déphasages entre sources et les modes ; la note d'interaction
les retire volontairement en comparant chaque place au chemin d'écran à cette
même place. Room Sim Pro note donc aussi la **couverture** : le niveau du
grave à chaque place face au niveau de la régie, couleur de la salle comprise
(rangs du fond qui perdent le grave, trous, boosts), bande par bande, avec la
même tolérance, les mêmes dB de doublement et la même audibilité. Pénalité de
la place = `1 − (1 − interaction) × (1 − w × couverture)`, avec `w` = 0,5 par
défaut (réglage Dev « Couverture (Pro), poids » ; 0 redonne l'interaction
seule). Au survol, chaque tête affiche son grave face à la régie.

**Étalonnage du système LFE.** Caissons et rappels LFE sont réglés ensemble,
comme le ferait un installateur : joués ensemble, ils atteignent le niveau de
la régie sur les places d'étalonnage. Un rappel déplace donc le grave vers
l'endroit où il est, il ne l'ajoute pas partout. Vérifié sur le podium du
showroom (20 × 10 × 4 m) : sans rappel, grave +0,8 dB devant et −2,7 dB au
fond face à la régie ; avec deux rappels LFE à mi-salle sur les côtés,
+0,4 / −1,7 dB, et la note de la salle passe de 76 à 80.

**Ce que reçoit chaque tête.** Pour chaque place : le niveau du grave de 20 à
120 Hz (dB SPL), sa sonie (phones de la bande la plus forte, ISO 226) et le
niveau sous 40 Hz, infra compris, ressenti plus qu'entendu. La luminosité de
chaque tête suit la sonie du grave qui l'atteint (sombre à 20 phones, pleine à
80) ; une note de 100 est en vert franc. Au survol : la note, l'écart, ce que
la tête entend et ce qu'elle ressent.

**Couleurs des places.** Une échelle à dix paliers (violet 0, magenta 40,
rouge 55, orange 65 à 75, jaune 82, vert-jaune 88, vert 93 à 97, turquoise
100) : des places à quelques points d'écart se distinguent. Au survol, la vue
3D donne la note de la place en % et l'écart en dB.

**Étalonnage.** Comme en vrai : on étalonne la salle installée normalement
(chaque enceinte à son niveau), puis les décalages de niveau réglés sur une
enceinte restent des décalages. Ainsi, des caissons montés de +6 dB comptent
bien (vérifié : écart médian −2,3 → +1,4 dB, note de la salle 51,6 → 54,3),
au lieu d'être effacés par l'étalonnage. La couleur propre de la salle n'est
toujours pas comptée ; un LFE décorrélé donne 0 dB partout, rappels compris
(vérifié).

**Score Room Sim Pro.** Chaque salle a une case « Counts ». Les salles cochées
(1, 2 ou 3) comptent comme des systèmes de lecture de plus, exactement comme
les salles cochées de Room Sim : la robustesse électrique (avant tout ajout de
Room Sim) est recalculée en gardant le pire cas. Chaque carte affiche la note
de la salle et le résultat = robustesse électrique × note de la salle / 100.

**Vue 3D.** Boîte grise translucide, mur avant (celui de l'écran) au fond avec
la toile en clair, ouverte vers l'observateur ; L, C, R en blanc, caissons en
orange, rappels en bleu, places colorées selon leur note. Glisser pour
tourner, molette pour zoomer, double-clic pour revenir à la vue de départ.

**Confort.** Un double-clic sur un potentiomètre le remet à sa valeur par
défaut, prise en proportion de la salle pour les positions (une enceinte au
milieu revient au milieu de la largeur réglée). Option-Z annule,
Option-Maj-Z rétablit, jusqu'à 30 pas (un geste continu compte pour un pas).
Changer un lien ou une disposition ne fait plus remonter la colonne des
réglages. Le drapeau en haut à gauche passe l'interface en français ou en
anglais ; la fenêtre se reconstruit dans la langue choisie, qui est gardée.

**Mémoire, export, import.** Les trois salles sont gardées entre deux
lancements (réglage `roomPro`), et « Save config... » / « Load config... »
écrivent et lisent un fichier `.subguardrooms`. Format texte, une ligne
`roomN.champ = valeur` par réglage sous l'en-tête `SubGUARD Room Sim Pro`,
version 4 (personnes, routage, nouvelles formes ; un fichier de version 3 qui avait le public en matériau de sol est converti) : positions en proportion de la salle (`roomN.L.x = 0.03`), liens
(`linkLR`, `linkFills`), mode de décroissance (`acoustics`), matériaux par leur
nom stable (`roomN.material.floor = concrete`), forme (`shape`), second jeu
L C R (`dualLCR`), disposition du public (`audienceLayout`, `aisle`,
`ringInner`, `ringOuter`). Un fichier de version 1
(positions en mètres) est converti à la lecture, liens coupés pour garder la
disposition telle quelle (vérifié). Un champ absent garde sa valeur par
défaut ; un fichier qui n'est pas un fichier Room Sim Pro est refusé. **Règle de
développement : tout nouveau réglage doit être écrit par `toText` et lu par
`fromText`.** Un test modifie chaque champ et vérifie l'aller-retour : un
réglage oublié dans l'export fait échouer les tests.

**Temps de calcul.** La préparation d'une salle (modes × sources × places) se
fait en arrière-plan, 0,25 s après le dernier changement de réglage ; une IMAX
de 96 places et 11 enceintes prend environ 0,4 s, les lieux hors cinéma 0,6 s
au plus. Les sources d'un même chemin sont additionnées mode par mode avant
la boucle des places, si bien qu'une place coûte quatre sommes sur les modes
quel que soit le nombre d'enceintes. Cas extrêmes : hangar de 60 × 40 × 25 m à
10 s de TR60 (27 000 modes, 96 places) 9 s, salle ronde de 40 m à 10 s 5 s ;
la carte affiche « computing... » pendant ce temps. La note se met ensuite à
jour à chaque image. Avec un TR60 long (jusqu'à 10 s), les modes deviennent très
étroits (largeur ≈ δ/π, 0,2 Hz à 10 s) : la grille d'analyse les échantillonne,
le résultat est une moyenne réaliste mais pas une courbe point par point.

## 4. Cadence d'analyse et inertie d'affichage

**Cadence.** L'analyse tourne dans son propre fil, à la cadence de l'audio : le
moteur reçoit chaque échantillon et s'analyse toutes les 20 ms d'audio
exactement, quelle que soit la taille des blocs de la carte son et quelle que
soit la fréquence d'affichage. Avec une fenêtre de Hann de 0,5, 1 ou 2 s, deux
analyses successives se recouvrent de 96, 98 ou 99 %. Tous les filtres et
calculs sont en double précision ; la carte son fournit du 32 bits flottant.
L'affichage (30 ou 60 Hz) ne touche jamais à l'audio : il lit le dernier état
complet publié par l'analyse, plus ce qui s'est accumulé depuis sa dernière
lecture (transitoires, crêtes des vumètres). Vérifié par les tests : même
résultat au bit près avec des blocs de 1, 517 ou 4 096 échantillons, et aucune
analyse perdue si l'affichage cesse de lire pendant 2 s.

**Mesure et affichage.** La mesure garde ses moyennes propres (Welch) : 150 ms
pour le spectre, 0,2 s pour les systèmes de lecture et le score, « Integration »
pour la corrélation. Tout le reste est de l'affichage et s'applique en aval,
sans toucher aux valeurs mesurées : le curseur « Inertia » (lissage du spectre
affiché, 10 à 600 ms), le filtre décrit ci-dessous, « Score speed », la
décroissance de l'alerte et le maintien des crêtes.

### Inertie d'affichage

Les chiffres et les courbes affichés (score, tableau des systèmes, niveaux,
courbes de corrélation) passent par un filtre qui regarde depuis combien de
temps la valeur reste du même côté de ce qui est affiché :

- elle repasse sans cesse de part et d'autre (elle papillonne) : le filtre est
  lent (constante de temps 1,5 s) et l'affichage se pose sur la moyenne ;
- elle reste du même côté (un vrai changement) : après 0,15 s le filtre
  accélère, et à 0,6 s il tourne à pleine vitesse (0,12 s).

Mesuré : une valeur qui oscille entre 0,1 et 0,9 dix fois par seconde fait
bouger l'affichage de 0,03 ; un saut de 1 à 2 est suivi à 90 % en 0,7 s.

La frise du bas montre les 2 min 30 dernières (une colonne par demi-seconde),
sur une échelle de temps compressée vers le passé (logarithmique, échelle
15 s) : les dernières secondes défilent vite à droite et prennent la plus
grande part de la largeur, les plus anciennes se tassent vers la gauche. Une
forme d'onde blanche discrète, comme dans un DAW, y montre ce qui a été joué
par-dessus les lignes d'alerte : crêtes positives et négatives et cœur RMS des
quatre canaux, 4 points par colonne, amplitude linéaire calée sur la plus
forte crête visible (zoom de 40 dB au plus).
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
14. ISO 226:2023, *Acoustics — Normal equal-loudness-level contours*
    (remplace l'édition 2003 ; formules (1) et (2), table 1, notes 1 à 3).
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
27. ISO 28961:2012, *Acoustics — Statistical distribution of hearing
    thresholds of otologically normal persons in the age range from 18 years
    to 25 years under free-field listening conditions*.
28. ISO 389-7:2019, *Acoustics — Reference zero for the calibration of
    audiometric equipment — Part 7: Reference threshold of hearing under
    free-field and diffuse-field listening conditions*.
29. B. R. Glasberg, B. C. J. Moore, « Derivation of auditory filter shapes from
    notched-noise data », *Hearing Research*, vol. 47, 1990.
30. Pedersen & Marquardt, 2009 : écart-type des seuils d'audition de l'ordre
    de 5 dB aux basses fréquences. Relevé par une recherche, texte non
    consulté (source secondaire).
31. H. Kuttruff, *Room Acoustics*, CRC Press (fréquence de Schroeder,
    statistique du champ réverbéré, somme des modes, amortissement des modes
    d'une salle rectangulaire en fonction de l'admittance des parois).
32. C. F. Eyring, « Reverberation time in "dead" rooms », *J. Acoust. Soc.
    Am.* 1, 217–241 (1930).
33. F. A. Everest, K. C. Pohlmann, *Master Handbook of Acoustics*,
    McGraw-Hill, table des coefficients d'absorption des matériaux courants
    (125 Hz à 4 kHz).
34. L. L. Beranek, *Concert Halls and Opera Houses*, Springer (absorption du
    public assis).
35. P. M. Morse, K. U. Ingard, *Theoretical Acoustics*, McGraw-Hill, 1968,
    §9.2 (modes d'une cavité cylindrique).
38. H. Møller, C. S. Pedersen, « Hearing at low and infrasonic frequencies »,
    *Noise & Health* 6(23), 37–57 (2004).
36. L. N. Trefethen, J. A. C. Weideman, « The exponentially convergent
    trapezoidal rule », *SIAM Review* 56(3), 385–458 (2014).
37. Y. Suzuki, H. Takeshima, K. Kurakata, « Revision of ISO 226 "Normal
    Equal-Loudness-Level Contours" from 2003 to 2023 edition: the background
    and results », *Acoust. Sci. & Tech.* 45(1), 2024.


## Niveau du LFE et réglages

**Case « LFE +10 dB ».** Elle s'applique partout : spectre, alertes, systèmes
de lecture et score. Cochée (défaut), le LFE compte 10 dB plus fort qu'un canal
d'écran, comme en salle [9]. Décochée, il est pris tel qu'enregistré.

**Panneau « Dev ».** Tout ce qui est un choix et non une mesure y est
réglable : tolérances des trois niveaux, poids d'un sub en retard ou en avance,
plafond de la dépendance au LFE, poids et bornes de l'infra-grave, poids de la
dépendance au niveau dans le score et moyenne à laquelle sa case est rouge.
