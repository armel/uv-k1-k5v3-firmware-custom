# CW Trainer 0.5

Application autonome Fusion **ABI1/API3 pour UV-K5 V3**.
[README principal en anglais](README.md) · [Validation détaillée](VALIDATION.md)

Audio local uniquement, aucune émission radio prévue. Les contrôles logiciels
ne prouvent pas définitivement l’absence physique de RF. F4LPF a testé la V0.3.3 sur un véritable UV-K5 V3 avec un paddle USB-C :
IAMB A/B, TX FREE, RX TRAIN, RX>TX, audio, décodage et ergonomie. Aucune véritable
pioche n’a été testée ; STRAIGHT reste validé uniquement en logiciel. Un second
récepteur sur la même fréquence n’a détecté aucune émission pendant les essais :
c’est un contrôle pratique, pas une mesure instrumentale ni une certification RF.

## Version 0.5

La version 0.5 ajoute toute la ponctuation et les prosignes (issue #722). TX FREE
et RX>TX les décodent, et les groupes de RX TRAIN / RX>TX puisent dans les 63
signes. Une suite d’éléments qui ne correspond à aucun signe s’affiche
désormais `*`, que le Morse n’émet jamais : un vrai `?` (`..--..`) n’est plus
ambigu.

- Ponctuation : `. , ? ' ! / ( ) & : ; = + - _ " $ @`
- Prosignes ayant un caractère : AR = `+`, AS = `&`, BT = `=`, KN = `(`
- Autres prosignes, affichés par un signe absent du Morse :

| Affiché | Prosigne | Morse | Sens |
|---|---|---|---|
| `[` | CT / KA | `-.-.-` | début de transmission |
| `]` | SK / VA | `...-.-` | fin de contact |
| `^` | VE / SN | `...-.` | compris |
| `~` | HH | `........` | erreur |
| `#` | SOS | `...---...` | détresse |
| `>` | CL | `-.-..-..` | fermeture de la station |
| `\|` | BK | `-...-.-` | break |
| `{` | AA | `.-.-` | nouvelle ligne |
| `}` | DO | `-..---` | passage au Wabun |

Décodage et lecture gèrent maintenant jusqu’à 9 éléments (SOS). Les tables
Morse passent dans les assets en flash externe, en accès direct (code vers
signe, signe vers code) : l’overlay descend à 4 060 o, les assets montent à
2 763 o sur 3 840.

## Version 0.4

La version 0.4 normalise le numéro de l’application au format `X.Y`. Le
comportement reste identique à celui de la version 0.3.3 ; les validations
logicielles et matérielles existantes décrivent donc toujours le même code.

## Correctif 0.3.3

En RX TRAIN, SIDE1 est désormais ignorée, même pendant une lecture.
`S1 NEW` disparaît ; `PTT RPLY` reste en petite police. **7 NEW** génère et joue
un nouveau groupe. SIDE1 en RX>TX et toutes les autres commandes restent
inchangées. Le comportement matériel signalé n’a entraîné aucune modification
ou recherche de correctif dans le firmware.

## Fonctions et installation

- TX FREE : pioche USB/PTT en STRAIGHT, paddle USB IAMB A/B, sidetone et décodage
  A–Z, 0–9, ponctuation et prosignes sur trois lignes avec défilement continu.
- RX TRAIN : groupes aléatoires cachés, écoute, prise de notes éventuelle sur
  papier, puis révélation. Aucune manipulation ni correction de réponse.
- RX>TX : écoute, reproduction, décodage, score positionnel et sons de résultat.
- Réglages RX/TX indépendants : 5–60 WPM, 300–1 000 Hz par 50 Hz ; groupes 1–5.
- GAP pédagogique partagé RX/RX>TX : 1×, 1,5×, 2×, 3× ; paramètres persistants.

Avec un firmware Fusion API3 compatible, installer **CWTrainer.app complet,
assets compris**, dans un slot libre avec UV Studio. L’ouvrir depuis le menu
Apps et vérifier la version 0.5 dans les informations du slot. Aucun changement
du firmware ni de l’enregistrement des applications.
Garder le binaire précédent pour retour arrière.

Compilation depuis la racine : `./compile-app.sh cwtrainer`, sortie
`build/Apps/CWTrainer.app`. Le script découvre automatiquement le dossier.
Sans Docker, avec `arm-none-eabi-gcc` dans le `PATH` :
`(cd App/apps/cwtrainer && ./build.sh)`, sortie dans le dossier de l’application.
Tests : `python App/apps/cwtrainer/test/run.py` (Python standard et GCC hôte).
Compilation mesurée avec ARM GCC 13.3.Rel1, comme le Dockerfile.

## Commandes

F+chiffre signifie F **puis** chiffre. Les chiffres et boutons latéraux agissent
une fois après 20 ms ; seules les flèches répètent (400 ms, puis 100 ms).

| Touche | Fonction |
|---|---|
| 0 | TX FREE → RX TRAIN → RX>TX |
| 1 | STRAIGHT/A/B en TX FREE et RX>TX ; désactivée en RX TRAIN |
| 2 / F+2 | WPM +1/−1 de la banque active ; conservée en RX TRAIN |
| 3 / F+3 | Tonalité +50/−50 Hz de la banque active |
| 4 / F+4 | Longueur +1/−1, modes RX uniquement |
| 5 | Inversion paddle, désactivée en RX TRAIN |
| 6 | CLEAR, désactivée en RX TRAIN |
| 7 | Nouveau groupe et écoute, modes RX uniquement |
| 8 | Cycle GAP, modes RX uniquement |
| 9 | Révéler en RX TRAIN ; corriger en RX>TX au repos |
| SIDE1 | Désactivée en RX TRAIN ; RX>TX : réécoute |
| PTT | RX TRAIN : réécoute ; ailleurs : pioche en STRAIGHT uniquement |
| UP/DOWN | WPM de la banque active, avec répétition |
| EXIT | Interrompre l’exercice/son ; nouvel appui après relâchement pour sortir |
| MENU | Réservée, pas de page d’aide |

RX TRAIN masque KEY/SWAP/CLEAR et ignore ces touches sans interrompre l’écoute.
WPM, tonalité, longueur, GAP, NEW, REPLAY et révélation restent disponibles.
RX TRAIN affiche uniquement `PTT RPLY` en police 3×5 ; SIDE1 est désactivée.
RX>TX conserve son indication de réécoute, avec LISTEN ; la cible révélée
et le texte CW gardent leur police normale. Autres éléments conservés.

En RX TRAIN, PTT sans cible ne génère rien. Un maintien ne répète pas la commande ;
un nouvel appui pendant la lecture la recommence proprement. Relâcher PTT après
lancement/changement de mode : un PTT maintenu ne doit pas agir à travers la
transition. Les réglages changent au repos. F+8 ne change pas le sens du cycle GAP.

Régler TX en TX FREE, puis RX dans les modes RX. RX>TX reproduit avec les réglages
TX et écoute avec RX. Le signe moins après WPM indique le préfixe F. Défauts :
18 WPM, TX 700 Hz, RX 600 Hz, longueur 5, GAP 1×. Sauvegarde différée à la sortie,
12/16 octets ; migration des anciens réglages 10 octets préservée.

GAP donne le silence **total** entre lettres : 3 / 4,5 / 6 / 9 unités, sans ajouter
deux fois le silence d’élément. DIT=1, DAH=3, silence intra-caractère=1 restent
inchangés, ainsi que le décodeur TX. Ce n’est pas un Farnsworth complet. Aucun
mot n’est généré ; une éventuelle extension devra multiplier aussi ses 7 unités.

## Mémoire, tests et limites

**Overlay 4 076/4 096 octets**, dont BSS **160** ; assets **1 488/3 840** ;
paquet complet **5 628** ; paramètres **12/16**. Marge overlay : 20 octets. Changer de
compilateur impose une nouvelle mesure. Assets de données, jamais de code.

**2 489 scénarios passent dans cinq variantes**, dont les tables API3
368/380/384 et les vrais corps audio résidents. Horodatage, latences 50/500 µs,
STRAIGHT/A/B, premier élément, débordement compteur, GAP, commandes physiques,
LCD, correction et absence de commande RF sont couverts. Les mêmes scénarios
sont répétés ; les résultats ne remplacent pas les essais matériels.

PTT/USB STRAIGHT sont combinés en OU ; PTT est ignoré en A/B pour préserver le
flux paddle. Adaptateur passif validé : D+/PA12 pointe, D−/PA11 anneau, GND,
VBUS/CC non connectés ; aucun PC/chargeur pendant le paddle. État USB restauré
à la sortie. Pioche limitée à 10 s, puis relâchement requis ; contacts relâchés
20 ms après changements. Défaut électrique USB après repos (#683) non corrigé.
Décodeur manuel à WPM fixe ; cible 5 caractères maximum, réponse d’exercice 16,
historique TX 33–48 caractères. Flash/LCD peuvent rallonger les silences à grande
vitesse. Pile réelle et RF à mesurer sur poste. Aucun lien avec la PR #699.

## Crédits et perspectives

- **F4LPF** : idée, conception fonctionnelle, ergonomie et essais matériels.
- **OpenAI Codex** : implémentation et tests automatisés.
- **ChatGPT** : assistance technique, conception et préparation de la contribution.
- Moteur/USB adaptés de CW USB/CW Keyer : **Armel F4HWN, Johan Denoyer F4WAT,
  Sébastien F4LPF**. Services et polices résidents inchangés ; notices et licence
  Apache-2.0 conservées, voir `LICENSE` à la racine du dépôt.

Perspective sans date ni garantie : trois applications indépendantes **CW Trainer
RX** (Koch/Farnsworth/groupes/indicatifs), **CW Trainer TX** (manipulation/décodage)
et **CW Trainer Virtual QSO** (QSO normal/contest RUN/S&P). Aucun découpage engagé.
