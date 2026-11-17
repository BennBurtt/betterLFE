# Fichiers de test 5.1

Le générateur `subguard_testfiles` écrit 22 fichiers WAV et mesure chacun avec le
moteur d'analyse. `./build_mac.sh` les crée dans `test-files/` (environ 300 Mo,
hors git), avec un `README.txt` qui reprend ce tableau.

- Format : 48 kHz, 24 bits, 5.1 dans l'ordre L R C LFE Ls Rs. Choisir « 5.1 ITU » dans l'app.
- Chaque fichier commence par une seconde de silence. Le lire en boucle donne une lecture plus stable.
- Réglages supposés : écoute à 85 dB(C), contrôle des transitoires allumé (panneau « Transients »), niveau « Strong », les deux polarités, case « LFE +10 dB » cochée.
- « Mesuré » : ce que le moteur lit lui-même en fin de fichier.

| Fichier | Contenu | À observer | Score mesuré | Transitoires mesurés |
|---|---|---|---|---|
| `01_clean_mix` | Mix propre : la façade porte le grave, LFE sans rapport et 10 dB dessous | Score proche de 100, aucune alerte | 100 | 3 attaques du mix, 2 dans le LFE : attaque du LFE vers -4 ms (son différent) |
| `02_lfe_copies_centre` | LFE = copie du centre, aussi fort à l'écoute | Score 0, enceinte C en alerte, heat-map ambre, courbe C à +1 | 0 | 2 attaques du mix, 2 dans le LFE : LFE -0.0 ms |
| `03_lfe_inverted_copy` | LFE = centre en polarité inversée | Score 0, heat-map rouge, courbe C à −1 ; alerte aussi en mode « Out of phase only » ; polarité de séance négative pour C | 0 | 2 attaques du mix, 2 dans le LFE : LFE -0.0 ms, inversé |
| `04_lfe_copy_5ms_late` | LFE = copie du centre, 5 ms en retard | Score bas ; la courbe C passe de +1 en bas à −1 vers 100 Hz | 0 | 2 attaques du mix, 2 dans le LFE : LFE +5.0 ms |
| `05_lfe_only` | Tout le grave dans le LFE, façade muette | Score 20–25, « LFE dependence » bas, aucune alerte de phase | 24 | — |
| `06_lfe_6dB_above_screen` | LFE sans rapport, 6 dB au-dessus de la façade | Score 20–30, ligne « LFE against the screen » en orange | 30 | 2 attaques du mix, 2 dans le LFE : attaque du LFE vers -4 ms (son différent) |
| `07_lfe_equal_unrelated` | LFE sans rapport, aussi fort que la façade | Score 60–70 : pas d'interaction, mais la moitié du grave repose sur le LFE | 73 | 2 attaques du mix, 2 dans le LFE : attaque du LFE vers -4 ms (son différent) |
| `08_lfe_partly_shared` | LFE 10 dB sous le centre, copié à 60 % sur lui | Score vers 60–65, « dangereux mais passable » ; courbe C vers +0,6 | 65 | 2 attaques du mix, 2 dans le LFE : LFE -0.4 ms |
| `09_per_channel` | L en phase avec le LFE, R hors phase, C sans rapport | Enceinte L en ambre, R en rouge, C au repos. Le score reste haut : ce que L ajoute, R le retire | 99 | 3 attaques du mix, 1 dans le LFE : attaque du LFE vers -2 ms (son différent) |
| `10_overlap_60_120` | Façade et LFE forts et sans rapport | Marques de recouvrement ambre sous 60–90 et 90–120 Hz | 73 | 2 attaques du mix, 2 dans le LFE : attaque du LFE vers -4 ms (son différent) |
| `11_transients_aligned` | Kicks au centre, LFE au même instant | Bandeau Transients : « Aligned, in phase » | 0 | 14 attaques du mix, 14 dans le LFE : LFE -0.0 ms |
| `12_transients_lfe_12ms_late` | Kicks au centre, LFE 12 ms en retard | « LFE late 12.0 ms » | 4 | 14 attaques du mix, 14 dans le LFE : LFE +12.0 ms |
| `13_transients_lfe_6ms_early` | Kicks au centre, LFE 6 ms en avance | « LFE early 6.0 ms » | 0 | 14 attaques du mix, 14 dans le LFE : LFE -6.0 ms |
| `14_transients_inverted` | Kicks au centre, LFE au même instant, polarité inversée | « Aligned, polarity inverted » | 0 | 14 attaques du mix, 14 dans le LFE : LFE +0.0 ms, inversé |
| `15_transients_very_quiet` | Comme le 12, mais 60 dB plus bas (crêtes vers −68 dBFS) | « LFE late 12.0 ms », identique au plein niveau | 98 | 14 attaques du mix, 14 dans le LFE : LFE +12.0 ms |
| `16_transients_low_boom_under_bed` | Booms à 28 Hz au centre sous un fond 60–180 Hz plus fort ; boom du LFE 8 ms en retard | « LFE late » vers 8–10 ms avec un ± large, ou « timing not measurable » | 62 | 15 attaques du mix, 13 dans le LFE : LFE +5.5 ms |
| `17_level_calibration` | Sinusoïdes : 40 Hz à −20 dBFS dans le LFE, 80 Hz à −20 dBFS au centre, 8 Hz à −30 dBFS dans le LFE | Au curseur : LFE −20,0 dB à 40 Hz, LCR −23,0 dB à 80 Hz (la somme d'écran est affichée 3 dB plus bas), LFE −30,0 dB à 8 Hz | 50 | 1 attaques du mix, 1 dans le LFE : attaque du LFE vers +0 ms (son différent) |
| `18_silence` | Silence numérique | Score « -- », rien ne bouge | -- | — |
| `19_music_kick_in_lfe_12ms_late` | Musique : ligne de basse (L, R et LFE), kick au centre (grave + clic), charleston à contretemps ; le LFE porte la basse et le kick, 12 ms en retard | Bandeau Transients : « LFE late » vers 12 ms à chaque kick. Avec Release baissé vers 10 %, les charlestons sont trouvés aussi : « nothing in the LFE » | 3 | 60 attaques du mix, 60 dans le LFE : LFE +12.0 ms |
| `20_music_kick_in_lfe_inverted` | Même musique ; kick du LFE à l'heure, polarité inversée | Les kicks sont dans le LFE : « Aligned, polarity inverted » | 5 | 60 attaques du mix, 60 dans le LFE : LFE +0.0 ms, inversé |
| `21_music_kick_not_in_lfe` | Même musique ; le LFE ne porte que la ligne de basse | Les attaques du mix sont comptées, aucune n'est dans le LFE | 4 | 60 attaques du mix, 1 dans le LFE : LFE +0.8 ms |
| `22_unlike_lfe_send_200ms_late` | Kick au-dessus de 60 Hz au centre ; le LFE répond par un coup à 30 Hz, 200 ms plus tard | « LFE attack late » vers 200 ms, daté d'attaque à attaque. Avec une plage de recherche sous 200 ms : « nothing in the LFE » | 69 | 14 attaques du mix, 14 dans le LFE : attaque du LFE vers +202 ms (son différent) |

La colonne « Transitoires mesurés » compte les attaques trouvées dans le
downmix L+C+R, puis celles que le LFE partage, avec le décalage médian du LFE.

Sur les fichiers de bruit tenu (01 à 10), le moteur compte quelques attaques :
le début du son, qui en est une, et de rares ondulations du bruit. Quand le LFE
est une copie de la façade, le verdict reste juste (aligné, inversé, ou 5 ms de
retard) ; quand il est sans rapport, seul le début du fichier est daté.
Mesures faites avec les réglages par défaut (plage de recherche ±250 ms).

Le fichier 15 (60 dB plus bas) affiche un score proche de 100 : à 85 dB(C), son
grave est sous le seuil d'audition, et un problème inaudible ne compte pas. Le
contrôle des transitoires, lui, lit le même retard qu'à plein niveau.
