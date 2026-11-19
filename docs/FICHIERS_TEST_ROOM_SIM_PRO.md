# Signaux de test Room Sim Pro

Quatorze signaux. Chacun isole **une seule chose**, pour voir ce que font les
deux lentilles de Room Sim Pro. Ils sont **dans l'app** : rien à télécharger.

## Mise en route

1. Carte **Source**, menu **périphérique d'entrée** : choisir **« Signaux de
   test SubGUARD »** (tout en bas de la liste). L'app joue alors ses propres
   signaux à la place d'une entrée audio, à 48 kHz. Pour revenir à ta carte
   son, la choisir dans la même liste.
2. Niveau **Strong**, case **LFE +10 dB** cochée (les mesures ci-dessous sont
   faites ainsi).
3. Onglet **Test** : bouton **« Charger les salles des mesures »**. Ce sont
   les trois salles mesurées ci-dessous : petite salle de projection,
   multiplexe 200 places, grand format premium (Option-Z pour revenir aux
   tiennes).
4. Cliquer un signal dans l'onglet Test : il joue en boucle. Ouvrir l'onglet
   **Room Sim Pro**, salle 2 (multiplexe 200 places) : les chiffres
   ci-dessous sont les siens.

Les mêmes signaux existent en fichiers WAV : `./build_mac.sh` les écrit dans
`test-files/room-sim-pro/`, avec `README.txt` (mesures des trois salles) et
`default_rooms.subguardrooms` (Fichier > Charger des salles…).

## Les deux lentilles en deux phrases

- **Lentille 1, « Qui a du grave, et combien »** : la sonie du grave à chaque
  tête, en phones, sur la même échelle que le mix validé (sa valeur est la
  marque blanche sur l'échelle). Filtre **Tout / LCR / LFE** : tout le grave,
  les LCR seuls ou le LFE seul.
- **Lentille 2, « Pourquoi »** : l'écart au mix validé, en dB, coupé en trois
  causes qui s'additionnent. **Salle** : ce que chaque enceinte donne seule à
  cet endroit (modes, distance). **Enceintes** : les interférences entre
  enceintes (lieu). **Mix** : l'interférence entre le LFE et les LCR (mix).
  Gris : rien ; bleu : la cause retire du grave ; rouge : elle en ajoute.

## Les signaux

Mesuré dans la salle 2, après un passage complet, par le moteur lui-même. « Têtes »
donne la plage des places ; les causes donnent la plage des places en dB.

| Signal | Contenu | Où regarder, ce qu'il faut voir | Mesuré (salle 2) |
|---|---|---|---|
| `P01_bass_L_only` | Du grave dans L seulement | **Lentille 1, LCR** : les têtes près de L sont les plus fortes. **LFE** : gris foncé, rien. **Lentille 2, Mix** : gris (pas de LFE, donc pas d'interférence LFE / LCR). **Enceintes** : gris (une seule enceinte). **Salle** : bleu et rouge, la salle seule | mix 51 phones ; têtes 38 à 59. Salle −6,0 à +5,3 dB ; enceintes 0 ; mix 0. Notes : répartition 60, placement 100, mix 100 |
| `P02_bass_C_only` | Du grave dans C seulement | Comme P01, autour de C | mix 49 ; têtes 35 à 55. Salle −5,4 à +4,5 ; enceintes 0 ; mix 0 |
| `P03_bass_R_only` | Du grave dans R seulement | Comme P01, en miroir : autour de R | mix 49 ; têtes 37 à 57. Salle −5,3 à +5,5 ; enceintes 0 ; mix 0 |
| `P04_bass_LR_same` | Le même grave dans L et R (basse mono en centre fantôme) | **Lentille 2, Enceintes** : bleu. Dans le mix validé, L et R s'additionnent exactement ; dans la salle, ils arrivent à des instants différents et s'additionnent moins. **Mix** : gris. C'est le lieu, pas le mix | Enceintes −5,1 à 0 dB (moyenne −2,6) ; mix 0. Note de placement **88** |
| `P05_bass_LR_unrelated` | Deux graves différents dans L et R | **Lentille 2, Enceintes** : presque gris, contrairement à P04 : deux signaux sans rapport ne peuvent pas interférer | Enceintes −0,7 à +0,1 dB. Note de placement 95 (inchangée : rien à pardonner) |
| `P06_lfe_only` | Tout le grave dans le LFE, écran muet | **Lentille 1, LFE** : là où portent les caissons. **LCR** : rien. **Lentille 2, Enceintes** : les deux caissons l'un contre l'autre. **Mix** : gris | mix 49 ; têtes 45 à 54. Enceintes −1,1 à +3,0 dB ; mix 0. Placement 83 |
| `P07_C_and_lfe_unrelated` | C et LFE aussi forts, sans rapport | **Lentille 2, Mix** : tout gris. Un LFE qui ne partage rien avec C ne peut ni s'y ajouter ni l'annuler | mix 0 partout. Note de mix 100 |
| `P08_lfe_copy_of_C` | Le LFE est une copie de C, 6 dB plus bas | **Lentille 2, Mix** : bleu. Dans le mix validé, C et LFE s'additionnent exactement ; dans la salle, caissons et C arrivent à des instants différents. C'est ce que l'assistant mixage travaillera | Mix −3,1 à −1,0 dB (moyenne −1,7). Note de mix **93** (un creux compte moins qu'une bosse) |
| `P09_lfe_copy_of_C_inverted` | La même copie, en polarité inversée | **Lentille 2, Mix** : rouge, l'inverse de P08. Dans le mix validé, le LFE inversé retire du grave à C ; dans la salle, il en retire moins | Mix +4,5 à +7,5 dB (moyenne +5,8). Note de mix **21** (des bosses : pas de marge) |
| `P10_lfe_copy_of_C_5ms_late` | La copie de C, 5 ms en retard | Dans le mix validé, 5 ms de retard annulent C vers 100 Hz (une demi-période). La salle défait en partie cette annulation : **lentille 2, Mix** rouge, et en **lentille 1**, des têtes plus fortes que le mix | mix 40 phones ; têtes 47 à 61. Mix −0,5 à +3,4 dB. Note de mix 86 |
| `P11_inaudible_bass` | C et LFE 60 dB plus bas (environ −86 dBFS) | **Lentille 1** : toutes les têtes gris foncé, 0 phone, comme le mix. **Lentille 2** : grise et plate, personne n'entend ce grave. Notes à 100 | mix 0 phone. Grave entendu : 6 % de chances au mieux. Répartition 99, placement 100, mix 100 |
| `P12_very_low_only` | Du grave dans C, de 20 à 60 Hz | **Vue en couches** : la couche du milieu (très grave) domine | couches du mix : très grave 33 phones, grave 21 |
| `P13_low_only` | Du grave dans C, de 60 à 120 Hz | **Vue en couches** : seule la couche du haut (grave) bouge | couches du mix : très grave 3 phones, grave 49 |
| `P14_infra_only` | Une sinusoïde à 12 Hz dans le LFE, rien d'autre | **Vue en couches** : seule la couche du bas (infra) bouge, en dB au-dessus du seuil (pas de phones sous 20 Hz). Les têtes restent gris foncé : rien n'est entendu de 20 à 120 Hz | infra du mix 6 dB au-dessus du seuil ; rien de 20 à 120 Hz |

## Ce que ces signaux apprennent

- **Une enceinte seule** (P01 à P03) : la lentille 2 ne montre que la salle.
  Enceintes et mix restent gris : il n'y a rien avec quoi interférer.
- **Deux enceintes qui jouent la même chose** (P04, P06) : la cause
  « Enceintes » s'allume. C'est le lieu : placement, délai, polarité.
- **Le même signal, sans rapport** (P05, P07) : rien ne s'allume. Deux sons
  sans rapport ne peuvent pas interférer.
- **Un LFE qui copie l'écran** (P08 à P10) : la cause « Mix » s'allume. C'est
  le mix : délai, polarité, niveau, coupure du LFE.
- **Un grave inaudible** (P11) : rien à montrer, comme il se doit.
- **Les couches** (P12 à P14) : chaque bande de fréquences a sa couche.
