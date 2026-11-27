# ANKS FX — suite d'effets ANKS Labs

23 petits effets audio VST3 pour Ableton Live (Lite compris), gratuits et open source.
Pas de fenêtre à ouvrir : **tous les réglages apparaissent directement dans le rack d'Ableton** (moins de 64 paramètres par effet, donc Live les affiche tout seul). La petite plaque du plug-in n'existe que parce que le format VST3 l'exige.

## ⬇ TÉLÉCHARGER ET INSTALLER — DOWNLOAD

[![TÉLÉCHARGER LA DERNIÈRE VERSION](https://img.shields.io/github/v/release/olivershadd-star/ANKS-FX-SUITE?include_prereleases&sort=date&display_name=tag&style=for-the-badge&logo=github&logoColor=F7FAFF&label=T%C3%89L%C3%89CHARGER&labelColor=10151E&color=EF343E)](../../releases/latest) [![TOUTES LES VERSIONS](https://img.shields.io/badge/TOUTES_LES-VERSIONS-F5C343?style=for-the-badge&labelColor=10151E)](../../releases)

1. Clique sur **TÉLÉCHARGER**, puis, dans la rubrique *Assets*, télécharge `ANKS-FX-Windows-x64-X.Y.Z.zip` (X.Y.Z = numéro de version : prends toujours le plus grand).
2. Clic droit sur le ZIP → **Extraire tout**.
3. Ferme Live et copie les dossiers `.vst3` dans `C:\Program Files\Common Files\VST3`.
4. Dans Live : **Préférences → Plug-ins → Dossier système VST3 = On**, puis **Relancer l'analyse**. Les effets apparaissent sous **Plug-ins → ANKS Labs**.

**EN —** Click **DOWNLOAD**, get `ANKS-FX-Windows-x64-X.Y.Z.zip` from *Assets*, extract it and copy the `.vst3` folders to `C:\Program Files\Common Files\VST3`, then rescan plug-ins.

---

## Les effets

| Effet | Ce qu'il fait | Boucles conseillées |
|---|---|---|
| **ANKS Ring** | Ring modulation / AM par un VCO (Sine, Triangle, Saw, Square). Key Follow : suit les notes MIDI. | Voix, synthés, cloches |
| **ANKS Shaper** | La boucle lit une table d'onde (Sines, Cheby, Folds, Steps) : distorsion qui change de grain avec Position. | Basses, leads |
| **ANKS Shift** | Frequency shifter : toutes les fréquences décalées du même nombre de Hz, feedback en spirale. | Batteries, pads |
| **ANKS Gate** | Gate rythmique euclidien calé sur le tempo, Chance pour des trous au hasard, swing. | Pads, nappes tenues |
| **ANKS Formant** | Filtre à voyelles A-E-I-O-U morphable, wah qui parle. | Basses, synthés |
| **ANKS PhaseMod** | Délai très court modulé par un VCO : flanger lent, jusqu'à une FM granuleuse en vitesse audio. | Guitares, arpèges |
| **ANKS Resonator** | 4 filtres en peigne accordés (note ou accord), Key Follow MIDI. | Percussions |
| **ANKS Grain** | Granulaire temps réel (tampon 4 s) : taille, densité, spray, pitch, reverse, Freeze. | Textures |
| **ANKS Stutter** | À chaque temps, une chance de répéter un morceau de boucle (pitch, reverse, fade). | Breakbeats |
| **ANKS Tape** | Cassette usée : wow, flutter, dérive, saturation, souffle, décrochages. | Loops mélodiques |
| **ANKS Crush** | Bitcrush + réduction d'échantillonnage, modulés par LFO, jitter. | Tout |
| **ANKS Triple Drive** | Trois distorsions en série (Soft / Hard / Fold), reprise du dépôt TripleDist. | Tout |
| **ANKS Tracker** | Le niveau de la boucle joue un oscillateur : sub sous le kick (Follow) ou notes déclenchées (Trigger, Random Notes). | Kicks, batteries |
| **ANKS Filter** | Filtre LP/BP/HP/Notch avec LFO et suiveur d'enveloppe. | Tout |

### Vague 2 — chaînes et déformations

| Effet | Ce qu'il fait | Boucles conseillées |
|---|---|---|
| **ANKS Double Filter** | Deux filtres multimodes en série ou en parallèle, chacun avec LFO et enveloppe. | Tout |
| **ANKS Triple Fold** | Trois wavefolders en série (suréchantillonnage x4), symétrie et tonalité par étage. | Synthés, basses |
| **ANKS Multiband Drive** | Graves / médiums / aigus distordus séparément (6 types), bande « Clean » possible. | Batteries, basses |
| **ANKS Sandwich** | Drive > filtre résonant (enveloppe, LFO) > Drive : résonance saturée, très acid. | Basses, lignes 303 |
| **ANKS Crush Drive** | Drive > bitcrush > Drive, modulé par LFO. | Batteries |
| **ANKS Saturation Chain** | Lampe > bande > transistor, Auto Gain et Tilt. | Tout, en finition |
| **ANKS Octave Fuzz** | Fuzz à redresseur : octave au-dessus, sub carré en dessous, gate qui crachote. | Basses, guitares |
| **ANKS Feedback Drive** | Distorsion renvoyée dans elle-même par un délai court : larsen, sifflements. Duck calme le jeu. | Tout (attention au volume) |
| **ANKS Exciter** | Sature seulement les aigus et les ajoute ; Air et Low Warmth. | Voix, hi-hats, mix |

Types de distorsion communs : Soft, Hard, Fold, Tube, Tape, Transistor.

Commun à tous : **Dry/Wet**, **Output**, **Bypass**. La plupart ont un **LFO** (Rate, Sync calé sur le tempo de Live, Shape dont S&H et Smooth Rnd) et une destination « LFO > … ».
Quand Live joue, le LFO et les effets rythmiques (Gate, Stutter) sont verrouillés sur la grille ; à l'arrêt, ils tournent au tempo de Live.

### Key Follow (Ring, Resonator, Tracker)
Ces trois effets acceptent le MIDI. Sur une piste MIDI, choisir « MIDI To » → la piste audio → l'effet. Activer **Key Follow** : la fréquence suit la note jouée.

## Compiler

Le workflow GitHub Actions compile les 23 effets à chaque push sur `main` (ou à la main depuis l'onglet Actions). Quand les 23 effets compilent et que le numéro de version change dans `CMakeLists.txt` (`project(ANKS_FX VERSION …)`), un ZIP numéroté contenant tous les `.vst3` est publié dans **Releases**. Si un effet échoue, rien n'est publié (l'artefact de test reste disponible dans Actions).

## Installer

Fermer Live, copier les dossiers `.vst3` dans `C:\Program Files\Common Files\VST3\`, relancer Live. Les effets apparaissent sous **ANKS Labs** dans les plug-ins.
Astuce : Ctrl+G sur l'effet pour le mettre dans un rack et mapper ses réglages aux macros.

## Remarque sur la Triple Drive

Même identifiant de plug-in que la version du dépôt TripleDist (code `Tdri`) et mêmes paramètres : les projets existants la retrouvent. Seule l'interface change (plaque minimale, réglages dans le rack).

## Licences

Code de la suite : MIT (voir LICENSE). Construit avec JUCE 8 (AGPLv3) : les binaires distribués gratuitement avec leur code source ouvert respectent ce cadre.
Sources des algorithmes : filtre d'état variable TPT d'Andrew Simper (Cytomic), coefficients du transformateur de Hilbert d'Olli Niemitalo, anti-repliement polyBLEP (Välimäki).
