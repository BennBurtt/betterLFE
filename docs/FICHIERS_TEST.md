# Fichiers de test 5.1

Le générateur `subguard_testfiles` écrit 18 fichiers WAV et mesure chacun avec le
moteur d'analyse. `./build_mac.sh` les crée dans `test-files/` (environ 300 Mo,
hors git), avec un `README.txt` qui reprend ce tableau.

- Format : 48 kHz, 24 bits, 5.1 dans l'ordre L R C LFE Ls Rs. Choisir « 5.1 ITU » dans l'app.
- Chaque fichier commence par une seconde de silence. Le lire en boucle donne une lecture plus stable.
- Réglages supposés : niveau « Strong », les deux polarités, case « LFE +10 dB » décochée.
- « Mesuré » : ce que le moteur lit lui-même en fin de fichier.

| Fichier | Contenu | À observer | Score mesuré | Transitoires mesurés |
|---|---|---|---|---|
| `01_clean_mix` | Mix propre : la façade porte le grave, LFE sans rapport et 10 dB dessous | Score proche de 100, aucune alerte | 100 | — |
| `02_lfe_copies_centre` | LFE = copie du centre, aussi fort à l'écoute | Score 0, enceinte C en alerte, heat-map ambre, courbe C à +1 | 0 | C: 3 coups, LFE -0.0 ms |
| `03_lfe_inverted_copy` | LFE = centre en polarité inversée | Score 0, heat-map rouge, courbe C à −1 ; alerte aussi en mode « Out of phase only » ; polarité de séance négative pour C | 0 | C: 3 coups, LFE -0.0 ms, inversé |
| `04_lfe_copy_5ms_late` | LFE = copie du centre, 5 ms en retard | Score bas ; la courbe C passe de +1 en bas à −1 vers 100 Hz | 0 | C: 3 coups, LFE +5.0 ms |
| `05_lfe_only` | Tout le grave dans le LFE, façade muette | Score 20–25, « LFE dependence » bas, aucune alerte de phase | 24 | — |
| `06_lfe_6dB_above_screen` | LFE sans rapport, 6 dB au-dessus de la façade | Score 20–30, ligne « LFE against the screen » en orange | 27 | — |
| `07_lfe_equal_unrelated` | LFE sans rapport, aussi fort que la façade | Score 60–70 : pas d'interaction, mais la moitié du grave repose sur le LFE | 70 | — |
| `08_lfe_partly_shared` | LFE 10 dB sous le centre, copié à 60 % sur lui | Score vers 60–65, « dangereux mais passable » ; courbe C vers +0,6 | 65 | C: 3 coups, LFE -0.4 ms |
| `09_per_channel` | L en phase avec le LFE, R hors phase, C sans rapport | Enceinte L en ambre, R en rouge, C au repos. Le score reste haut : ce que L ajoute, R le retire | 99 | L: 3 coups, LFE +0.7 ms; R: 3 coups, LFE -0.4 ms, inversé |
| `10_overlap_60_120` | Façade et LFE forts et sans rapport | Marques de recouvrement ambre sous 60–90 et 90–120 Hz | 70 | — |
| `11_transients_aligned` | Kicks au centre, LFE au même instant | Bandeau Transients, C : « Aligned, in phase » | 0 | C: 14 coups, LFE -0.0 ms |
| `12_transients_lfe_12ms_late` | Kicks au centre, LFE 12 ms en retard | C : « LFE late 12.0 ms » | 5 | C: 14 coups, LFE +12.0 ms |
| `13_transients_lfe_6ms_early` | Kicks au centre, LFE 6 ms en avance | C : « LFE early 6.0 ms » | 0 | C: 14 coups, LFE -6.0 ms |
| `14_transients_inverted` | Kicks au centre, LFE au même instant, polarité inversée | C : « Aligned, polarity inverted » | 0 | C: 14 coups, LFE +0.0 ms, inversé |
| `15_transients_very_quiet` | Comme le 12, mais 60 dB plus bas (crêtes vers −68 dBFS) | C : « LFE late 12.0 ms », identique au plein niveau | 5 | C: 14 coups, LFE +12.0 ms |
| `16_transients_low_boom_under_bed` | Booms à 28 Hz au centre sous un fond 60–180 Hz plus fort ; boom du LFE 8 ms en retard | C : « LFE late » vers 8–10 ms avec un ± large, ou « timing not measurable » | 58 | C: 14 coups, LFE +10.1 ms |
| `17_level_calibration` | Sinusoïdes : 40 Hz à −20 dBFS dans le LFE, 80 Hz à −20 dBFS au centre, 8 Hz à −30 dBFS dans le LFE | Au curseur : LFE −20,0 dB à 40 Hz, LCR −23,0 dB à 80 Hz (la somme d'écran est affichée 3 dB plus bas), LFE −30,0 dB à 8 Hz | 35 | — |
| `18_silence` | Silence numérique | Score « -- », rien ne bouge | -- | — |
| `18 files and README.txt written to /` | Silence numérique | Score « -- », rien ne bouge | tmp/claude-0/-home-user-betterlfe/114ecf6f-41d6-525e-8c8d-460cc9303e4e/scratchpad/testfiles | — |

Les fichiers 02 à 04, 08 et 09 signalent quelques « coups » alors qu'ils ne
contiennent que du bruit tenu : le détecteur prend le début du son et certaines
ondulations du bruit pour des attaques. Le verdict affiché reste juste (aligné,
inversé, ou 5 ms de retard), puisque le LFE y est bien une copie de la façade.
