# EditorUpec 2020™

EditorUpec 2020™ est un multi-éditeur de texte permettant d'éditer jusqu'à 10 fichiers texte depuis le même terminal.

## Installation

Récupérer tous les fichiers présents sur le git. Attention, il est très important que le fichier EditorUpec-v0.1.hlp soit présent, il est nécessaire pour la commande :help (:h)

```invite de commande de l'editeur
:help
```
La resolution minimale pour utiliser l'editeur est 65x20 (une résolution plus basse empêche le démarrage du programme).

## Conception
Un journal relatant toute l'histoire du developpement du logiciel est présent dans projet.log.
EditorUpec 2020™ est inspiré de KILO (https://viewsourcecode.org/snaptoken/kilo/) qui m'a aidé a démarrer (notamment pour les structures et fonctions de base et la gestion du terminal), puis une fois lancé, je me suis inspiré de VIM en redéveloppant certaines de ses fonctionnalités.
La gestion de curseur est inspirée de VIM (gestion des lignes longues sur plusieurs lignes écran, contrairement au scroll horizontal de KILO).

## Usage

Lancer la commande ./projet pour lancer un éditeur vierge
```
./projet
```
Lancer la commande ./projet nomDeFichier pour lancer l'edition du fichier nomDeFichier
```
./projet nomDeFichier
```
Lancer la commande ./projet nomDeFichier1 nomDeFichier2 nomDeFichier3... pour ouvrir les différents fichiers dans le multi-editeur (10 éditeurs disponibles)
```
./projet nomDeFichier1 nomDeFichier2 nomDeFichier3
```

## Fonctions

- EditorUpec 2020™ permet d'ouvrir des fichiers texte, et de les éditer.
- EditorUpec 2020™ peut aussi être lancé sans fichier, et crée alors un éditeur vierge.
- Le programme permet de sauvegarder son travail a tout moment. Quand on le ferme, un warning est affiché si l'on a pas encore sauvé ses modifications.
- EditorUpec 2020™ propose une invite de commande permettant d'accéder à ses différentes fonctions.
- la Liste des différentes commandes possibles est accessible en entrant h ou help dans l'invite de commandes (accessible en tapant :). Alternativement, on peut accéder à la page d'aide avec CTRL H.
- Une fonction recherche et remplacement est accessible en tapant / (fonctionnement expliqué dans la page d'aide).
- une page d'information sur l'état du multi-editeur est disponible avec la commande :bi (pour buffer info).
- une ligne de service proposant diverses informations est affichée en bas de l'écran (descriptif détaillé de la ligne de service dans la page d'aide).
- EditorUpec 2020™ gère la souris : il est possible de deplacer le curseur avec la souris, le clic gauche fixe le curseur sur le caractère pointé, ou le plus proche.

## Problèmes et solutions
Le problème majeur rencontré a été de gérer correctement à l'écran l'affichage des lignes longues dans l'éditeur qui nécessitaient plusieurs lignes d'affichage à l'écran. Il s'agissait notamment de synchroniser les déplacements du curseur écran avec l'évolution du caractère courant dans la structure de l'éditeur.
Le même problème s'est posé avec la sélection d'un caractère lors d'un clic souris.

Ces problèmes ont été résolus avec la construction de 2 fonctions qui calculent:
-la position relative du curseur écran en fonction de la position absolue du caractère courant dans l'éditeur.
-la position absolue dans l'éditeur en fonction de la position du curseur souris à l'écran.

Une autre difficulté a été de fiabiliser l'affichage lors de suppressions, insertions ou déplacements en bordure de pages.

(plus amples informations dans le fichier .log)

## Limites
- caractères non unicode non gérées (on ne peut pas les insérer, leur lecture pose problème pour la synchronisation du curseur).
- caractère Tabulation non géré (simulation avec des espaces).
- Recherche/remplacement : on ne peut pas inclure de '/' dans les motifs de recherche et remplacement car il sert de séparateur de champs. Je  n'ai pas géré de possibilité d'échappement, par exemple en préfixant avec le caractère '\', ce qui compliquerait beaucoup le parsing.

(autres limites diverses dans le fichier .log)

## Contribuer
Le projet est distribué "tel quel", toutefois , au vu de la licence de ce projet, il est possible de faire absolument tout et n'importe quoi avec.

## Auteurs
Valentin Lafitau, L2 informatique, Upec

## Remerciements
Salvatore Sanfilippo (a.k.a antirez) pour le code open source, et le tutoriel sur son éditeur de texte KILO.
Pascal Vanier pour son PDF de cours, ainsi que son aide sur Discord.
Mes parents, pour leurs "bêta tests" ainsi que leurs conseils.


## License
[UNLICENSE](https://choosealicense.com/licenses/unlicense/)

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <https://unlicense.org>
