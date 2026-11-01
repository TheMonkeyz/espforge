// Display texts: one line per text, one column per language (forge_i18n.h: English, French). Included several
// times with different X() definitions (app_text.h, app_text.c): no include guard. French is Canadian French
// (Québec), standard written. Every text needs every language, with the same printf conversions in the same order.

// The hello and system pages
X(T_HELLO,          "Hello",                       "Bonjour")
X(T_HELLO_SUB,      "espforge, no Wi-Fi",          "espforge, sans Wi-Fi")
X(T_SWIPE_HINT,     "Swipe left: system\nLong-press: settings",
                    "Glissez à gauche : système\nAppuyez longuement : réglages")
X(T_SYSTEM,         "System",                      "Système")
X(T_SYS_VERSION,    "Version %s",                  "Version %s")
X(T_SYS_MEMORY,     "Memory %u KB  ·  PSRAM %u KB","Mémoire %u Ko  ·  PSRAM %u Ko")
X(T_SYS_UPTIME,     "Up %s",                       "En marche depuis %s")

// Settings screen (forge_settings: the app's word for each settings_text_t code, ui.c set_texts). The Wi-Fi
// starter's words; the codes of rows this app can't have (phone page, Wi-Fi, updates, network: forge_net and
// forge_ota aren't in the build) get T_SET_NONE.
X(T_SET_NONE,       "-",                           "-")
X(T_SET_DONE,       "Done",                        "OK")
X(T_SET_SEC_SCREEN, "SCREEN",                      "ÉCRAN")
X(T_SET_DIM_QUIET,  "Dim when quiet",              "Tamiser si calme")
X(T_SET_WAKE_PICKUP,"Wake on pick-up",             "Réveil si soulevé")
X(T_SET_TIMING,     "Timing",                      "Délais")
X(T_SET_SHORT,      "Short",                       "Courts")     // agrees with "Délais" (plural)
X(T_SET_NORMAL,     "Normal",                      "Normaux")
X(T_SET_LONG,       "Long",                        "Longs")
X(T_SET_CUSTOM,     "Custom",                      "Autres")
X(T_SET_BRIGHTNESS, "Brightness %d%%",             "Luminosité %d %%")
X(T_SET_LANGUAGE,   "Language",                    "Langue")
X(T_SET_SEC_MORE,   "MORE",                        "PLUS")
X(T_SET_TAP_AGAIN,  "Tap again",                   "Touchez encore")
X(T_SET_RESTART,    "Restart",                     "Redémarrer")
X(T_SET_RESTARTING, "Restarting...",               "Redémarrage...")
X(T_SET_SEC_ABOUT,  "ABOUT",                       "À PROPOS")
X(T_SET_VERSION,    "Version",                     "Version")
X(T_SET_MEMORY,     "Memory",                      "Mémoire")
X(T_SET_MEMORY_KB,  "%u / %u KB",                  "%u / %u Ko")
X(T_SET_UPTIME,     "Running for",                 "En marche depuis")
X(T_SET_UPTIME_MIN, "%d min",                      "%d min")
X(T_SET_UPTIME_H,   "%d h %02d",                   "%d h %02d")
X(T_SET_UPTIME_D,   "%d d %d h",                   "%d j %d h")
