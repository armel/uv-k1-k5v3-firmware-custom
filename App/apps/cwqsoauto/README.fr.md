# CW QSO Auto — notice française

Application overlay autonome pour **UV-K5 V3**, firmware F4HWN compatible ABI 1 /
API 3. Le paquet vise l'archive `v6.3.0`. La validation matérielle rapportée
concerne l'UV-K5 V3 ; l'UV-K1 n'a pas été qualifié ici.

[Documentation anglaise](README.md) · [Architecture](ARCHITECTURE.md) ·
[Tests et limites](TESTING.md)

## Personnaliser sans compiler

1. Télécharger [editor.html](editor.html), puis ouvrir ce fichier local dans
   un navigateur récent. Le `.app` de référence est **déjà embarqué** : les
   16 champs se remplissent sans importer de fichier et sans accès réseau.
2. Remplacer `F4XXX` par son indicatif dans MSG 01 et 02, et `SEB` par son
   prénom si nécessaire. Ces valeurs génériques sont seulement les valeurs
   initiales ; les textes personnalisés restent libres dans l'alphabet permis.
   Vérifier les messages avant un QSO réel.
3. Modifier les textes, puis cliquer sur **Vérifier et télécharger**. Alphabet :
   `A–Z`, chiffres, espace, `/`, `?` et jeton exact `{RST}`. Chaque emplacement
   accepte 64 octets utiles ; `{RST}` ne consomme qu'un octet. Pas d'espace
   initial/final/doublé, de conversion automatique ni de troncature. Un champ
   vide désactive ce message.
4. Conserver le `.app` téléchargé : le réimporter dans le HTML permet de
   retrouver les 16 textes. La fermeture de la page ne sauvegarde rien
   automatiquement. L'éditeur ne relit pas la Flash du poste.

Les anciens profils Bank16 autorisés restent importables, **sans changement de
leur code ni de leurs commandes**. Importer un ancien `.app` n'installe donc pas
automatiquement la répétition CQ. Pour reprendre ses textes avec le nouveau
code, utiliser deux onglets : ancien fichier dans l'un, référence embarquée
dans l'autre ; copier les textes puis télécharger depuis la référence.

Le fichier indépendant
[`CWQSOAuto.app`](../../../archive/apps/v6.3.0/CWQSOAuto.app) et le fichier
embarqué sont identiques avant personnalisation. Installer de nouveau la
référence par défaut ne conserve pas les anciens messages personnalisés.

## Installer avec UV Studio

Quitter l'application du poste avant de connecter l'ordinateur au port USB-C.
Ouvrir [UV Studio](https://armel.github.io/uvstudio/), aller dans la vue
Applications / Apps, connecter le port série, choisir son `.app` local et
l'installer avec les commandes d'application. Utiliser un firmware compatible
de la série 6.3.0 : ce fichier est une **application**, pas un firmware `.bin`.
Déconnecter ensuite l'ordinateur avant de brancher l'adaptateur paddle USB-C.
Lancer **CW QSO Auto** dans le menu Apps du poste.

## Utilisation

| Commande | Effet sur l'écran QSO |
| --- | --- |
| UP / DOWN | Navigation circulaire entre MSG 01 et MSG 16 |
| PTT | Une émission du message sélectionné, sans confirmation ; maintien sans répétition |
| SIDE 1 | MSG 01 immédiatement, puis répétition avec trois secondes de pause après la dernière marque de chaque message |
| SIDE 2 | Termine le message déjà commencé puis arrête ; pendant la pause, supprime le prochain départ |
| EXIT / paddle | Annulation immédiate de la séquence automatique par le chemin RX normal, avec relâchement obligatoire |
| Chiffres | Saisie glissante du RST sur trois chiffres ; `599` à l'ouverture |
| MENU | Accès à CW SETTINGS |

Les nouveaux appuis SIDE 1 ne s'empilent pas. Pendant la pause, PTT transmet la
sélection courante et reprend la main. MSG 15 `AGN?` et MSG 16 `?` restent
accessibles par navigation/PTT. Les MSG 04 à 16 affichent un aperçu borné du
contenu réel : `*` représente `{RST}`, `>` signale une suite non affichée.
Le texte stocké et transmis n'est pas tronqué.

Il n'y a plus d'armement TIP de cinq secondes, ni d'affichage LOCK/READY.
Une commande déjà maintenue à l'ouverture est consommée jusqu'au relâchement
qualifié. Les modes STRAIGHT, AUTO, IAMB A et IAMB B restent disponibles.

CW SETTINGS : `1` STRAIGHT/PADDLE, `2` vitesse 5–60 WPM, `3` hauteur audio,
`4` break-in, `5` inversion du paddle, `6` correction du quartz, `8` AUTO/A/B,
`0` haut-parleur, UP/DOWN fréquence. `F` sélectionne la diminution pour le
prochain réglage numérique. MENU/EXIT revient à QSO ; EXIT depuis QSO au repos
quitte l'application. Le RST est distinct des réglages CW sauvegardés.

## Architecture et limites

L'overlay occupe **4 092 / 4 096 octets, dont 92 de BSS**. Les **2 282 octets
d'assets** restent en Flash externe ; les 16 messages sont des enregistrements
fixes de 68 octets, dont 64 de texte. `{RST}` est l'octet `02`, développé avec
le report courant lors de l'émission. L'éditeur modifie ces données et leur
CRC, jamais le code exécutable.

La configuration sur ordinateur évite d'ajouter un éditeur clavier dans les
4 Kio du poste ou de modifier le firmware résident. Le HTML contient le
binaire pour fournir un point de départ cohérent, utilisable hors ligne.
Ses profils de code et d'assets sont stricts ; un fichier inconnu, corrompu ou
hors format est refusé, y compris lorsque son CRC a été recalculé.

La préparation HP anticipée traite le premier DIT localement muet après RX :
Tone1 à fréquence nulle, sélection AF_ALAM puis activation HP avant l'attente
PLL existante de 2 ms. Aucun tampon temporel ni préambule supplémentaire de
10/16 ms n'est intégré. La calibration VCO, les rampes RF et le retour RX sont
conservés. Un très léger clic de commutation a été rapporté pendant le
développement ; aucune absence totale de clic n'est garantie ici.

La marge mémoire n'est que de quatre octets. La compilation de référence est
GCC ARM 13.3.Rel1 ; un autre compilateur peut dépasser la limite. Les relais
Thumb sont compatibles Cortex-M0+ et leurs offsets ABI sont vérifiés à la
compilation. Les résultats logiciels et les essais matériels rapportés sont
distingués dans [TESTING.md](TESTING.md).

Licence : conditions Apache-2.0 existantes du dépôt, attributions d'origine
conservées. Aucun nouveau régime de licence.
