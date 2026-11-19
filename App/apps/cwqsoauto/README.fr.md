# CW QSO Auto v1.0 — notice française

Application CW autonome pour UV-K5 V3, firmware F4HWN ABI 1/API 3 minimum.
Compilation et tests logiciels effectués sur `feature_update_v7` (API 4).
La version stable a été validée par son auteur sur UV-K5 V3 ; aucun nouvel
essai matériel V7 n’est revendiqué. [English](README.md) · [Tests](TESTING.md)

## Personnalisation

Ouvrir [editor.html](editor.html) comme fichier local dans un navigateur récent.
Le binaire actuel est embarqué : aucun serveur, réseau ou compilateur nécessaire.
Chacun des 16 messages possède un numéro fixe, un nom de 0 à 12 caractères ASCII
visibles et un contenu CW de 64 octets encodés maximum. Le nom n’est jamais émis.
Alphabet CW : A–Z, chiffres, espace, `/`, `?`, jeton `{RST}` (un octet).
Pas d’espaces en début/fin ou doublés. Un contenu vide désactive le message.
Remplacer F4XXX et SEB ; CALL et NAME sont du texte, pas des jetons dynamiques.

Exporter le .app personnalisé et l’installer avec la vue Applications d’UV Studio.
Le conserver pour sauvegarde : sa réimportation restaure noms et contenus.
Fermer l’éditeur ne sauvegarde pas automatiquement. Réinstaller les valeurs par
défaut remplace les personnalisations. L’éditeur ne lit pas le poste.

La seule référence actuelle à installer est
[`archive/apps/v6.3.0/CWQSOAuto.app`](../../../archive/apps/v6.3.0/CWQSOAuto.app),
identique au binaire embarqué. Ce répertoire suit la convention actuelle du dépôt
V7. Les anciens profils approuvés gardent leur code lors d’un import, sans
migration automatique ; ceux sans noms restent éditables pour leurs contenus.

## Commandes

| Touche | Écran principal |
| --- | --- |
| UP/DOWN | Parcourir les 16 messages en boucle |
| PTT | Envoyer une fois le message sélectionné |
| SIDE 1 | Répéter le message sélectionné au lancement |
| SIDE 2 | Arrêter après la fin du message courant ; pendant la pause, annuler le prochain départ |
| EXIT ou paddle pendant un message | Annuler immédiatement l’envoi automatique |
| Chiffres | Saisie glissante du RST, initialement 599 |
| MENU | Ouvrir SETTINGS |
| EXIT au repos | Quitter et sauvegarder les réglages |

MSG 01 est sélectionné au départ. Changer la sélection pendant une pause ne
change pas le message répété. Un PTT pendant la pause arrête la répétition et
envoie la sélection courante. SIDE 1 n’empile pas les demandes. Les 16 messages
affichent numéro, nom et aperçu : `*` représente RST, `>` la suite non affichée.
MENU est en bas à gauche ; la ligne d’aide de prévisualisation reste masquée.

Dans SETTINGS : UP/DOWN règle REPEAT de 1 à 15 s, défaut 5 s, sauvegardé ;
1 bascule STRAIGHT/PADDLE ; 8 choisit AUTO/IAMB A/IAMB B ; 2 règle les WPM
(5–60), 3 la tonalité, 4 le break-in, 5 l’inversion, 6 la calibration XTAL,
0 le HP. F puis une touche de réglage numérique diminue la valeur.
PTT fonctionne comme une pioche : maintien de la porteuse jusqu’au relâchement,
avec rampes, refus TX et protection de durée maximale inchangés.
MENU/EXIT revient à l’écran principal. Relâcher les commandes au démarrage
et après une annulation ; aucun armement TIP de cinq secondes n’est requis.

## Construction et limites

Avec ARM_CC pointant vers GCC ARM 13.3.Rel1 :
`python App/apps/cwqsoauto/build.py`. Le point d’entrée compile-app est build.sh.
Overlay **4084/4096 octets, dont 92 BSS**, assets **2286 octets**.
Le binaire est identique au tag stable `cw-qso-auto-v1.0-stable` ; son en-tête
reste en version 0.4 pour préserver le fichier validé. v1.0 désigne la version
fonctionnelle stable. Aucun changement du résident ou des autres applications.

Le premier DIT est audible grâce à l’activation anticipée du HP. Un léger clic
initial subsiste, compromis assumé lors des essais de l’auteur. L’absence de
clic sur V7 et la qualification matérielle UV-K1 ne sont pas revendiquées.
