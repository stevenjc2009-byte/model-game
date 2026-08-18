#include <3ds.h>

#include "strings.h"
#include "settings.h"

// One row per stringId, one column per gameLanguage. Kept as two arrays
// rather than a single { id, en, fr } struct list so a translator (or a
// script) can diff one language's column against another without English
// getting in the way, and so STR_COUNT rows in each array is a compile-time
// promise the two cannot silently drift apart in length.

static const char* const englishText[STR_COUNT] =
{
	[STR_TITLE]        = "MODEL KIT",
	[STR_TAGLINE]      = "snip  -  file  -  click it together",
	[STR_PLAY]         = "Play",
	[STR_OPTIONS]      = "Options",
	[STR_QUIT]         = "Quit",

	[STR_VOLUME]       = "Master volume",
	[STR_MINUS]        = "-",
	[STR_PLUS]         = "+",
	[STR_CONTROLS]     = "Controls",
	[STR_BACK]         = "Back",
	[STR_VOL_NOTE]     = "The master level for every sound the game plays.",
	[STR_LANGUAGE]     = "Language",
	[STR_RESET]        = "Reset",

	[STR_UPD_LINE1]    = "Check for",
	[STR_UPD_LINE2]    = "Update",
	[STR_UPD_CHECK]    = "Check now",
	[STR_UPD_GET]      = "Download",
	[STR_UPD_RESTART]  = "Restart",
	[STR_UPD_OFF]      = "Unavailable",
	[STR_UPD_SHORT_CHECKING]    = "Checking...",
	[STR_UPD_SHORT_UP_TO_DATE]  = "Up to date",
	[STR_UPD_SHORT_AVAILABLE]   = "Update available",
	[STR_UPD_SHORT_DOWNLOADING] = "Downloading...",
	[STR_UPD_SHORT_INSTALLING]  = "Installing...",
	[STR_UPD_SHORT_DONE]        = "Installed",
	[STR_UPD_SHORT_FAILED]      = "Could not update",
	[STR_UPD_SHORT_READY]       = "Ready",
	[STR_UPD_INSTALLED_ROW]     = "Installed  %s",
	[STR_UPD_NEWEST_ROW]        = "Newest     %s",

	[STR_LEVELS_HDR]   = "Choose a level",
	[STR_PREV]         = "<",
	[STR_NEXT]         = ">",
	[STR_BUILT_TAG]    = "built",

	[STR_LISTENING]    = "press a button",
	[STR_CANCEL_HINT]  = "B cancels",

	[STR_CTL_TAP_KEY]    = "Stylus tap",       [STR_CTL_TAP_ACT]    = "snip / fit / tap again to undo",
	[STR_CTL_RUB_KEY]    = "Stylus rub",       [STR_CTL_RUB_ACT]    = "file the selected part's nub",
	[STR_CTL_DRAG_KEY]   = "Stylus drag / Pad",[STR_CTL_DRAG_ACT]   = "turn the bench",
	[STR_CTL_DPADLR_KEY] = "D-Pad Left/Right", [STR_CTL_DPADLR_ACT] = "page the manual",
	[STR_CTL_B_KEY]      = "B",                [STR_CTL_B_ACT]      = "back to the step you are on",
	[STR_CTL_LR_KEY]     = "L / R",            [STR_CTL_LR_ACT]     = "zoom out / in",
	[STR_CTL_A_KEY]      = "A",                [STR_CTL_A_ACT]      = "reset the view",
	[STR_CTL_YX_KEY]     = "Y / X",            [STR_CTL_YX_ACT]     = "photo mode / next angle",
	[STR_CTL_DDOWN_KEY]  = "D-Pad Down",       [STR_CTL_DDOWN_ACT]  = "pose the model (once built)",
	[STR_CTL_DUP_KEY]    = "D-Pad Up",         [STR_CTL_DUP_ACT]    = "save a screenshot to the SD",
	[STR_CTL_SELECT_KEY] = "SELECT",           [STR_CTL_SELECT_ACT] = "pause menu",
	[STR_CTL_START_KEY]  = "START",            [STR_CTL_START_ACT]  = "close the game",

	[STR_KEY_A]      = "A",       [STR_KEY_B]      = "B",
	[STR_KEY_X]      = "X",       [STR_KEY_Y]      = "Y",
	[STR_KEY_L]      = "L",       [STR_KEY_R]      = "R",
	[STR_KEY_DUP]    = "D-Up",    [STR_KEY_DDOWN]  = "D-Down",
	[STR_KEY_DLEFT]  = "D-Left",  [STR_KEY_DRIGHT] = "D-Right",
	[STR_KEY_SELECT] = "SELECT",  [STR_KEY_START]  = "START",

	[STR_C_LEVELS_HDR]   = "Model Kit  -  levels %d to %d\n",
	[STR_C_LEVEL_ROW]    = "  Level %2d   %2d parts   %s\n",
	[STR_C_BUILT_TOTAL]  = "  Built %d of %d.\n",
	[STR_C_LEVELS_HELP1] = "Tap a number to start it. A kit counts as built once\nits last part is fitted; the tick stays on the tile.\n",
	[STR_C_LEVELS_HELP2] = "< and > page through the twenty, Back returns.\n",

	[STR_C_OPTIONS_HDR]   = "Model Kit  -  options\n",
	[STR_C_VOLUME_LINE]   = "Master volume : %d%%\n",
	[STR_C_OPTIONS_HELP1] = "Sets the level for every sound the game plays.\nSilent on an emulator with no DSP firmware.\n",
	[STR_C_OPTIONS_HELP2] = "Tap - and + to change it, Controls for the button\nlist, or Back to return to the menu.\n",
	[STR_C_OPTIONS_HELP3] = "Check for Update asks GitHub whether a newer\nrelease of the game exists. Needs wifi.\n",

	[STR_C_UPDATE_HDR]      = "Model Kit  -  update\n",
	[STR_C_INSTALLED_LINE]  = "Installed version : %s\n",
	[STR_C_NEWEST_LINE]     = "Newest release    : %s\n",
	[STR_C_UPD_UNAVAILABLE1] = "The updater could not start.\n",
	[STR_C_UPD_UNAVAILABLE2] = "This build cannot install titles - that needs the\n",
	[STR_C_UPD_UNAVAILABLE3] = ".cia, launched from the HOME menu, on a console\nrunning custom firmware.\n",
	[STR_C_UPD_PROGRESS]     = "  %d%%\n\n",
	[STR_C_UPD_HELP_AVAILABLE1] = "Tap Download to fetch and install it. The game\n",
	[STR_C_UPD_HELP_AVAILABLE2] = "closes and comes back as the new version.\n",
	[STR_C_UPD_HELP_BUSY1]      = "Leave the console alone until this finishes.\n",
	[STR_C_UPD_HELP_BUSY2]      = "Closing the game now would leave a broken title.\n",
	[STR_C_UPD_HELP_DONE]       = "Tap Restart to start the new version.\n",
	[STR_C_UPD_HELP_DEFAULT]    = "Tap Check now to ask again, or Back to return.\n",
	[STR_C_UPD_NOT_SET]         = "not set",

	[STR_C_CONTROLS_HDR]  = "Model Kit  -  controls\n",
	[STR_C_CONTROLS_ROW]  = "  %-18s %s\n",
	[STR_C_CONTROLS_HELP] = "These work on the workbench, once you tap Play.\n",

	[STR_C_TITLE_HDR]   = "Model Kit\n",
	[STR_C_TITLE_HELP1] = "A model kit, a cutting mat and a pair of nippers.\n",
	[STR_C_TITLE_HELP2] = "Snip the parts off the runner, file the nubs\n",
	[STR_C_TITLE_HELP3] = "smooth, and click them together on the stand.\n",
	[STR_C_HARDWARE_LINE] = "Hardware : %s\n",
	[STR_C_HARDWARE_NEW]  = "New 3DS / New 2DS XL",
	[STR_C_HARDWARE_OLD]  = "Original 3DS / 2DS",
	[STR_C_TITLE_HELP4]   = "Use the stylus on the bottom screen.\n",
	[STR_C_TITLE_ROW_PLAY]    = "  Play     choose a level and start building\n",
	[STR_C_TITLE_ROW_OPTIONS] = "  Options  master volume, and the controls\n",
	[STR_C_TITLE_ROW_QUIT]    = "  Quit     close the game\n",

	[STR_C_BEGINNER_HEADER]      = "FIRST BUILD - ONE STEP AT A TIME",
	[STR_C_BEGINNER_STEP]        = "STEP %d OF 4",
	[STR_C_BEGINNER_DONE]        = "DONE",
	[STR_C_BEGINNER_TITLE_OPEN]  = "OPEN THE KIT BOX",
	[STR_C_BEGINNER_TITLE_JOIN]  = "TAP THE SMALL JOIN",
	[STR_C_BEGINNER_TITLE_RUB]   = "RUB THE LOOSE PART",
	[STR_C_BEGINNER_TITLE_TAP]   = "TAP THE AMBER SHAPE",
	[STR_C_BEGINNER_SUB_OPEN]    = "Tap the red mark on the lid to lift it off.",
	[STR_C_BEGINNER_SUB_JOIN]    = "Tap the red dot on the small gate.",
	[STR_C_BEGINNER_SUB_RUB]     = "Keep rubbing until the nub is smooth.",
	[STR_C_BEGINNER_SUB_TAP]     = "The part clicks into its final place.",
	[STR_C_BEGINNER_KIT_CAPTION] = "%s  -  BOX / RUNNER %d OF %d",
	[STR_C_BEGINNER_STYLUS_HINT] = "Use the stylus on the bottom screen.",
	[STR_C_BEGINNER_COMPLETE]    = "COMPLETE THIS STEP TO CONTINUE",

	[STR_C_SEAT_NONE]              = "- - -",
	[STR_C_SEAT_ON_RUNNER]         = "still on the runner",
	[STR_C_SEAT_FITTED_TAP_REMOVE] = "fitted - tap to remove",
	[STR_C_SEAT_FILE_FIRST]        = "%s - file the nub first",
	[STR_C_SEAT_TAP_SOCKET]        = "%s - tap the socket",
	[STR_C_SEAT_ALREADY_FITTED]    = "%s is already fitted",
	[STR_C_SEAT_TAKES]             = "%s takes %s",
	[STR_C_SEAT_NEEDS_FIRST]       = "%s needs %s first",
	[STR_C_SEAT_SNIP_FIRST]        = "%s - snip it off first",
	[STR_C_SEAT_FITTED]            = "fitted - %s",
	[STR_C_SEAT_HOLDS]             = "%s holds %s",
	[STR_C_SEAT_TAKEN_OFF]         = "taken off - %s",
	[STR_C_STATUS_POSE_HINT]       = "%s - L/R to pose",
	[STR_C_STATUS_TAP_MOVES]       = "tap a part that moves",
	[STR_C_STATUS_RAM]             = "RAM %u%%",

	[STR_C_PHOTO_HEADER] = "\n  Photo mode\n\n",
	[STR_C_PHOTO_NEXT]   = "    X   next angle\n",
	[STR_C_PHOTO_BACK]   = "    Y   back to the build\n",

	[STR_C_MANUAL_GUIDE]  = "  Snip the stub, file the nub, tap the ghost.\n",
	[STR_C_MANUAL_HEADER] = "Model Kit  -  build manual       level %2d\n",

	[STR_T_STEP_N]      = "STEP %d",
	[STR_T_OF_4]        = "OF 4",
	[STR_T_OPEN_TITLE]  = "OPEN THE BOX",
	[STR_T_OPEN_SUB]    = "Touch the red dot on the lid.",
	[STR_T_OPEN_LIFTS]  = "it lifts off",
	[STR_T_SNIP_COUNT]  = "%d of %d done",
	[STR_T_SNIP_TITLE]  = "SNIP EVERY PART OFF",
	[STR_T_SNIP_SUB]    = "Touch the red dot on the little bridge.",
	[STR_T_SNIP_BRIDGE] = "this bridge holds it on",
	[STR_T_SNIP_FREE]   = "it comes free",
	[STR_T_SNIP_ALL_TEN]= "DO ALL TEN:",
	[STR_T_FILE_PCT]    = "this part %d%%",
	[STR_T_FILE_TITLE]  = "RUB THE BUMP FLAT",
	[STR_T_FILE_SUB]    = "Slide the stylus side to side across the bump.",
	[STR_T_FILE_BUMP]   = "the bump",
	[STR_T_FILE_SMOOTH] = "flat and smooth",
	[STR_T_FILE_KEEP]   = "keep rubbing",
	[STR_T_FIT_COUNT]   = "%d of %d built",
	[STR_T_FIT_TITLE]   = "PUT IT WHERE IT GLOWS",
	[STR_T_FIT_SUB]     = "Touch the orange see-through shape.",
	[STR_T_FIT_HOLDING] = "you are holding",
};

// French. Button-name entries (A, B, SELECT, START, D-Pad ...) are left
// identical to English on purpose - they are what is silkscreened on the
// hardware, not prose, and translating "SELECT" would make the Controls
// page describe a button that is not there. Everything else is translated.
//
// Accents are allowed on exactly one half of this table, and which half is
// decided by where the string is drawn, not by how it reads:
//
//   citro2d (bottom screen, and the illustrated top page)  -  accents fine.
//     C2D_TextParse decodes UTF-8 and the system shared font has the whole of
//     Latin-1, so "découper" comes out as eight glyphs with the accent on the
//     e.  STR_TITLE .. STR_CANCEL_HINT and every STR_T_ row are drawn this way.
//
//   the console (printf, and everything liveRow() puts on the top screen)  -
//     ASCII only.  consoleInit uses libctru's default font: 128 glyphs, plain
//     ASCII, and no UTF-8 decoding whatsoever.  "Découpez" printed there comes
//     out "D|-coupez" - the two bytes of the e-acute are drawn as two separate
//     box-drawing glyphs, so the accent is unreadable AND every column after it
//     on that line is pushed one place right, which wrecks the aligned tables
//     on the manual and controls pages.  This is measured, not assumed: a
//     forced-French build was booted and the top screen photographed.
//     STR_C_, STR_CTL_ and STR_KEY_ are all printed, so all three stay ASCII.
//
// So: a new string keeps its accents only if nothing ever passes it to printf.
static const char* const frenchText[STR_COUNT] =
{
	[STR_TITLE]        = "MAQUETTE",
	[STR_TAGLINE]      = "découper  -  limer  -  assembler",
	[STR_PLAY]         = "Jouer",
	[STR_OPTIONS]      = "Options",
	[STR_QUIT]         = "Quitter",

	[STR_VOLUME]       = "Volume général",
	[STR_MINUS]        = "-",
	[STR_PLUS]         = "+",
	[STR_CONTROLS]     = "Commandes",
	[STR_BACK]         = "Retour",
	[STR_VOL_NOTE]     = "Le niveau de tous les sons du jeu.",
	[STR_LANGUAGE]     = "Langue",
	[STR_RESET]        = "Réinit.",

	[STR_UPD_LINE1]    = "Vérifier",
	[STR_UPD_LINE2]    = "la mise à jour",
	[STR_UPD_CHECK]    = "Vérifier",
	[STR_UPD_GET]      = "Télécharger",
	[STR_UPD_RESTART]  = "Redémarrer",
	[STR_UPD_OFF]      = "Indisponible",
	[STR_UPD_SHORT_CHECKING]    = "Vérification...",
	[STR_UPD_SHORT_UP_TO_DATE]  = "À jour",
	[STR_UPD_SHORT_AVAILABLE]   = "Mise à jour dispo",
	[STR_UPD_SHORT_DOWNLOADING] = "Téléchargement...",
	[STR_UPD_SHORT_INSTALLING]  = "Installation...",
	[STR_UPD_SHORT_DONE]        = "Installée",
	[STR_UPD_SHORT_FAILED]      = "Échec",
	[STR_UPD_SHORT_READY]       = "Prêt",
	[STR_UPD_INSTALLED_ROW]     = "Installée   %s",
	[STR_UPD_NEWEST_ROW]        = "Dernière    %s",

	[STR_LEVELS_HDR]   = "Choisir un niveau",
	[STR_PREV]         = "<",
	[STR_NEXT]         = ">",
	[STR_BUILT_TAG]    = "fini",

	[STR_LISTENING]    = "appuyez sur un bouton",
	[STR_CANCEL_HINT]  = "B annule",

	[STR_CTL_TAP_KEY]    = "Toucher stylet",   [STR_CTL_TAP_ACT]    = "decouper / fixer / annuler",
	[STR_CTL_RUB_KEY]    = "Frotter stylet",   [STR_CTL_RUB_ACT]    = "limer la piece choisie",
	[STR_CTL_DRAG_KEY]   = "Glisser / Pad",    [STR_CTL_DRAG_ACT]   = "tourner l'etabli",
	[STR_CTL_DPADLR_KEY] = "Croix Gauche/Droite", [STR_CTL_DPADLR_ACT] = "tourner les pages",
	[STR_CTL_B_KEY]      = "B",                [STR_CTL_B_ACT]      = "revenir a l'etape en cours",
	[STR_CTL_LR_KEY]     = "L / R",            [STR_CTL_LR_ACT]     = "zoom arriere / avant",
	[STR_CTL_A_KEY]      = "A",                [STR_CTL_A_ACT]      = "reinitialiser la vue",
	[STR_CTL_YX_KEY]     = "Y / X",            [STR_CTL_YX_ACT]     = "mode photo / angle suivant",
	[STR_CTL_DDOWN_KEY]  = "Croix Bas",        [STR_CTL_DDOWN_ACT]  = "poser le modele (une fois fini)",
	[STR_CTL_DUP_KEY]    = "Croix Haut",       [STR_CTL_DUP_ACT]    = "capture d'ecran sur la carte SD",
	[STR_CTL_SELECT_KEY] = "SELECT",           [STR_CTL_SELECT_ACT] = "menu pause",
	[STR_CTL_START_KEY]  = "START",            [STR_CTL_START_ACT]  = "fermer le jeu",

	[STR_KEY_A]      = "A",              [STR_KEY_B]      = "B",
	[STR_KEY_X]      = "X",              [STR_KEY_Y]      = "Y",
	[STR_KEY_L]      = "L",              [STR_KEY_R]      = "R",
	[STR_KEY_DUP]    = "Croix Haut",     [STR_KEY_DDOWN]  = "Croix Bas",
	[STR_KEY_DLEFT]  = "Croix Gauche",   [STR_KEY_DRIGHT] = "Croix Droite",
	[STR_KEY_SELECT] = "SELECT",         [STR_KEY_START]  = "START",

	[STR_C_LEVELS_HDR]   = "Model Kit  -  niveaux %d a %d\n",
	[STR_C_LEVEL_ROW]    = "  Niveau %2d   %2d pieces   %s\n",
	[STR_C_BUILT_TOTAL]  = "  Termines : %d sur %d.\n",
	[STR_C_LEVELS_HELP1] = "Touchez un numero pour commencer. Un kit est fini\nquand sa derniere piece est posee ; la coche reste.\n",
	[STR_C_LEVELS_HELP2] = "< et > tournent les pages, Retour revient au menu.\n",

	[STR_C_OPTIONS_HDR]   = "Model Kit  -  options\n",
	[STR_C_VOLUME_LINE]   = "Volume general : %d%%\n",
	[STR_C_OPTIONS_HELP1] = "Regle le niveau de tous les sons du jeu.\nMuet sur un emulateur sans le DSP.\n",
	[STR_C_OPTIONS_HELP2] = "Touchez - et + pour regler, Commandes pour la\nliste des boutons, ou Retour pour revenir au menu.\n",
	[STR_C_OPTIONS_HELP3] = "Verifier la mise a jour interroge GitHub. Wifi\nrequis.\n",

	[STR_C_UPDATE_HDR]      = "Model Kit  -  mise a jour\n",
	[STR_C_INSTALLED_LINE]  = "Version installee : %s\n",
	[STR_C_NEWEST_LINE]     = "Derniere version   : %s\n",
	[STR_C_UPD_UNAVAILABLE1] = "La mise a jour n'a pas pu demarrer.\n",
	[STR_C_UPD_UNAVAILABLE2] = "Cette version ne peut pas installer de titre - il\n",
	[STR_C_UPD_UNAVAILABLE3] = "faut le .cia, lance depuis le menu HOME, sur une\nconsole avec un firmware personnalise.\n",
	[STR_C_UPD_PROGRESS]     = "  %d%%\n\n",
	[STR_C_UPD_HELP_AVAILABLE1] = "Touchez Telecharger pour l'installer. Le jeu\n",
	[STR_C_UPD_HELP_AVAILABLE2] = "se ferme et revient dans la nouvelle version.\n",
	[STR_C_UPD_HELP_BUSY1]      = "Ne touchez pas a la console avant la fin.\n",
	[STR_C_UPD_HELP_BUSY2]      = "Fermer le jeu maintenant abimerait le titre.\n",
	[STR_C_UPD_HELP_DONE]       = "Touchez Redemarrer pour lancer la nouvelle version.\n",
	[STR_C_UPD_HELP_DEFAULT]    = "Touchez Verifier pour reessayer, ou Retour.\n",
	[STR_C_UPD_NOT_SET]         = "non definie",

	[STR_C_CONTROLS_HDR]  = "Model Kit  -  commandes\n",
	[STR_C_CONTROLS_ROW]  = "  %-18s %s\n",
	[STR_C_CONTROLS_HELP] = "Actifs sur l'etabli, une fois Jouer touche.\n",

	[STR_C_TITLE_HDR]   = "Model Kit\n",
	[STR_C_TITLE_HELP1] = "Une maquette, un tapis de coupe et des pinces.\n",
	[STR_C_TITLE_HELP2] = "Decoupez les pieces de la grappe, limez les\n",
	[STR_C_TITLE_HELP3] = "carottes, et assemblez-les sur le support.\n",
	[STR_C_HARDWARE_LINE] = "Materiel : %s\n",
	[STR_C_HARDWARE_NEW]  = "New 3DS / New 2DS XL",
	[STR_C_HARDWARE_OLD]  = "3DS / 2DS d'origine",
	[STR_C_TITLE_HELP4]   = "Utilisez le stylet sur l'ecran du bas.\n",
	[STR_C_TITLE_ROW_PLAY]    = "  Jouer    choisir un niveau et commencer\n",
	[STR_C_TITLE_ROW_OPTIONS] = "  Options  volume general, et les commandes\n",
	[STR_C_TITLE_ROW_QUIT]    = "  Quitter  fermer le jeu\n",

	[STR_C_BEGINNER_HEADER]      = "PREMIERE MAQUETTE - PAS A PAS",
	[STR_C_BEGINNER_STEP]        = "ETAPE %d SUR 4",
	[STR_C_BEGINNER_DONE]        = "FAIT",
	[STR_C_BEGINNER_TITLE_OPEN]  = "OUVREZ LA BOITE",
	[STR_C_BEGINNER_TITLE_JOIN]  = "TOUCHEZ L'ATTACHE",
	[STR_C_BEGINNER_TITLE_RUB]   = "FROTTEZ LA PIECE",
	[STR_C_BEGINNER_TITLE_TAP]   = "TOUCHEZ LA CIBLE",
	[STR_C_BEGINNER_SUB_OPEN]    = "Touchez le point rouge sur le couvercle.",
	[STR_C_BEGINNER_SUB_JOIN]    = "Touchez le point rouge de l'attache.",
	[STR_C_BEGINNER_SUB_RUB]     = "Frottez jusqu'a lisser la carotte.",
	[STR_C_BEGINNER_SUB_TAP]     = "La piece s'enclenche a sa place finale.",
	[STR_C_BEGINNER_KIT_CAPTION] = "%s  -  BOITE / GRAPPE %d SUR %d",
	[STR_C_BEGINNER_STYLUS_HINT] = "Utilisez le stylet sur l'ecran du bas.",
	[STR_C_BEGINNER_COMPLETE]    = "TERMINEZ L'ETAPE POUR CONTINUER",

	[STR_C_SEAT_NONE]              = "- - -",
	[STR_C_SEAT_ON_RUNNER]         = "encore sur la grappe",
	[STR_C_SEAT_FITTED_TAP_REMOVE] = "fixe - touchez pour retirer",
	[STR_C_SEAT_FILE_FIRST]        = "%s - limez d'abord",
	[STR_C_SEAT_TAP_SOCKET]        = "%s - touchez le point",
	[STR_C_SEAT_ALREADY_FITTED]    = "%s est deja fixe",
	[STR_C_SEAT_TAKES]             = "%s recoit %s",
	[STR_C_SEAT_NEEDS_FIRST]       = "%s : %s d'abord",
	[STR_C_SEAT_SNIP_FIRST]        = "%s - a decouper d'abord",
	[STR_C_SEAT_FITTED]            = "fixe - %s",
	[STR_C_SEAT_HOLDS]             = "%s retient %s",
	[STR_C_SEAT_TAKEN_OFF]         = "retire - %s",
	[STR_C_STATUS_POSE_HINT]       = "%s - L/R pour poser",
	[STR_C_STATUS_TAP_MOVES]       = "touchez une piece mobile",
	[STR_C_STATUS_RAM]             = "RAM %u%%",

	[STR_C_PHOTO_HEADER] = "\n  Mode photo\n\n",
	[STR_C_PHOTO_NEXT]   = "    X   angle suivant\n",
	[STR_C_PHOTO_BACK]   = "    Y   retour a la construction\n",

	[STR_C_MANUAL_GUIDE]  = "  Decoupez l'ergot, limez, touchez le fantome.\n",
	[STR_C_MANUAL_HEADER] = "Model Kit  -  manuel de montage    niveau %2d\n",

	[STR_T_STEP_N]      = "ÉTAPE %d",
	[STR_T_OF_4]        = "SUR 4",
	[STR_T_OPEN_TITLE]  = "OUVREZ LA BOÎTE",
	[STR_T_OPEN_SUB]    = "Touchez le point rouge du couvercle.",
	[STR_T_OPEN_LIFTS]  = "il se soulève",
	[STR_T_SNIP_COUNT]  = "%d sur %d faits",
	[STR_T_SNIP_TITLE]  = "DÉCOUPEZ CHAQUE PIÈCE",
	[STR_T_SNIP_SUB]    = "Touchez le point rouge de l'attache.",
	[STR_T_SNIP_BRIDGE] = "cette attache la retient",
	[STR_T_SNIP_FREE]   = "elle se détache",
	[STR_T_SNIP_ALL_TEN]= "LES DIX :",
	[STR_T_FILE_PCT]    = "cette pièce %d%%",
	[STR_T_FILE_TITLE]  = "LIMEZ LA BOSSE",
	[STR_T_FILE_SUB]    = "Frottez le stylet d'un côté à l'autre sur la bosse.",
	[STR_T_FILE_BUMP]   = "la bosse",
	[STR_T_FILE_SMOOTH] = "plate et lisse",
	[STR_T_FILE_KEEP]   = "frottez encore",
	[STR_T_FIT_COUNT]   = "%d sur %d montés",
	[STR_T_FIT_TITLE]   = "POSEZ-LA OÙ ÇA BRILLE",
	[STR_T_FIT_SUB]     = "Touchez la forme orange transparente.",
	[STR_T_FIT_HOLDING] = "vous tenez",
};

static const char* const* const tables[LANG_COUNT] =
{
	[LANG_ENGLISH] = englishText,
	[LANG_FRENCH]  = frenchText,
};

const char* languageName(gameLanguage lang)
{
	switch (lang)
	{
		// Drawn by citro2d (title.c), never printed to the console, so this one
		// can carry its cedilla where the STR_C_ rows below cannot.
		case LANG_FRENCH: return "Français";
		default:          return "English";
	}
}

gameLanguage languageCurrent(void)
{
	int lang = settingsLanguage();

	// -1 is settings.c's "never chosen" sentinel - a fresh install, or a save
	// from before this setting existed. Rather than opening on English for a
	// player whose console has never spoken English, ask the console once
	// here and persist the answer, so this only runs the one time.
	if (lang < 0)
	{
		gameLanguage detected = languageDetectSystem();
		settingsSetLanguage((int)detected);
		return detected;
	}

	if (lang >= LANG_COUNT) return LANG_ENGLISH;
	return (gameLanguage)lang;
}

void languageSet(gameLanguage lang)
{
	if (lang < 0 || lang >= LANG_COUNT) lang = LANG_ENGLISH;
	settingsSetLanguage((int)lang);
}

gameLanguage languageDetectSystem(void)
{
	// Self-contained: opens cfg, asks it one question, closes it again. The
	// cfg service is refcounted by libctru, so this is safe to call even if
	// something else in the process already has it open, and it leaves
	// nothing running afterwards for main.c to have to know about.
	gameLanguage result = LANG_ENGLISH;
	if (R_FAILED(cfguInit())) return result;

	u8 sysLang = 1; // CFG_LANGUAGE_EN
	if (R_SUCCEEDED(CFGU_GetSystemLanguage(&sysLang)))
	{
		// CFG_LANGUAGE_FR is 2 in libctru's cfg.h. Only French is shipped
		// beyond English, so that is the only code worth matching - every
		// other system language falls back to English, which is already
		// the default.
		if (sysLang == 2) result = LANG_FRENCH;
	}

	cfguExit();
	return result;
}

const char* STR(stringId id)
{
	static const char* const badId = "?";
	if (id < 0 || id >= STR_COUNT) return badId;

	gameLanguage lang = languageCurrent();
	const char* s = tables[lang][id];
	if (s && s[0]) return s;

	// Empty slot in a non-English column - fall back one language at a time,
	// ending at English, which every slot above fills in.
	s = tables[LANG_ENGLISH][id];
	return (s && s[0]) ? s : badId;
}
