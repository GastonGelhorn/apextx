/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

// DE translations author: Helmut Renz
// German checked 28.08.2019 r158 opentx V2.3.0 für X12S,X10,X9E,X9D+,X9D,QX7 X9Lite,XLite

/*
 * Formatting octal codes available in TR_ strings:
 *  \037\x           -sets LCD x-coord (x value in octal)
 *  \036             -newline
 *  \035             -horizontal tab (ARM only)
 *  \001 to \034     -extended spacing (value * FW/2)
 *  \0               -ends current string
 */

// Main menu
#define TR_QM_MANAGE_MODELS            "Modell\nManager"
#define TR_QM_MODEL_SETUP              "Modell\nSetup"
#define TR_QM_RADIO_SETUP              "Sender\nSetup"
#define TR_QM_UI_SETUP                 "UI\nSetup"
#define TR_QM_TOOLS                    "Tools"
#define TR_QM_MODEL_SETTINGS           "Modell\nEinst."
#define TR_QM_RADIO_SETTINGS           "Sender\nEinst."
#define TR_QM_FLIGHT_MODES             TR_SFC_AIR("Fahr-\nModi", "Flug-\nModi")
#define TR_QM_INPUTS                   "Geber"
#define TR_QM_MIXES                    "Mischer"
#define TR_QM_OUTPUTS                  "Ausgänge"
#define TR_QM_CURVES                   "Kurven"
#define TR_QM_GLOBAL_VARS              "Glob.\nVariab."
#define TR_QM_LOGICAL_SW               "Logik-\nSchalter"
#define TR_QM_SPEC_FUNC                "Spez.\nFunktn."
#define TR_QM_CUSTOM_LUA               "Eigene\nSkripte"
#define TR_QM_TELEM                    "Telemetrie"
#define TR_QM_GLOB_FUNC                "Glob.\nFunktn."
#define TR_QM_TRAINER                  "Trainer"
#define TR_QM_HARDWARE                 "Hardware"
#define TR_QM_ABOUT                    "Über\nEdgeTX"
#define TR_QM_THEMES                   "Themes"
#define TR_QM_TOP_BAR                  "Infozeile"
#define TR_QM_SCREEN_1                 "Seite 1"
#define TR_QM_SCREEN_2                 "Seite 2"
#define TR_QM_SCREEN_3                 "Seite 3"
#define TR_QM_SCREEN_4                 "Seite 4"
#define TR_QM_SCREEN_5                 "Seite 5"
#define TR_QM_SCREEN_6                 "Seite 6"
#define TR_QM_SCREEN_7                 "Seite 7"
#define TR_QM_SCREEN_8                 "Seite 8"
#define TR_QM_SCREEN_9                 "Seite 9"
#define TR_QM_SCREEN_10                "Seite 10"
#define TR_QM_ADD_SCREEN               "Neue\nSeite"
#define TR_QM_APPS                     "Apps"
#define TR_QM_STORAGE                  "Speicher"
#define TR_QM_RESET                    TR_SFC_AIR("Fahrt-\nReset", "Flug-\nReset")
#define TR_QM_CHAN_MON                 "Kanal-\nMonitor"
#define TR_QM_LS_MON                   "LS-\nMonitor"
#define TR_QM_STATS                    "Statistiken"
#define TR_QM_DEBUG                    "Debug"
#define TR_MAIN_MODEL_SETTINGS         "Modell-Einstellungen"
#define TR_MAIN_RADIO_SETTINGS         "Sender-Einstellungen"
#define TR_MAIN_MENU_MANAGE_MODELS     "Modell Manager"
#define TR_MAIN_MENU_MODEL_NOTES       "Modell Notizen"
#define TR_MAIN_MENU_CHANNEL_MONITOR   "Kanal Monitor"
#define TR_MONITOR_SWITCHES            "Logik-Schalter Monitor"
#define TR_MAIN_MENU_MODEL_SETTINGS    "Modell Setup"
#define TR_MAIN_MENU_RADIO_SETTINGS    "Sender Setup"
#define TR_MAIN_MENU_SCREEN_SETTINGS   "UI Setup"
#define TR_MAIN_MENU_STATISTICS        "Statistiken"
#define TR_MAIN_MENU_ABOUT_EDGETX      "Über EdgeTX"
#define TR_MAIN_VIEW_X                 "Seite "
#define TR_MAIN_MENU_THEMES            "Themes"
#define TR_MAIN_MENU_APPS              "Apps"
#define TR_MENUHELISETUP               TR_BW_COL("HELI EINST.", "Helikopter Einstellungen")
#define TR_MENUFLIGHTMODES             TR_SFC_AIR(TR_BW_COL("FAHRMODI", "Fahrmodi"), TR_BW_COL("FLUGPHASEN", "Flugphasen"))
#define TR_MENUFLIGHTMODE              TR_SFC_AIR(TR_BW_COL("FAHRMODUS", "Fahrmodus"), TR_BW_COL("FLUGPHASE", "Flugphase"))
#define TR_MENUINPUTS                  TR_BW_COL("GEBER", "Geber") //Inputs=Geber/Eingänge which is also used at Multiplex/Jeti
#define TR_MENULIMITS                  TR_BW_COL("AUSGÄNGE", "Ausgänge")  //OUTPUTS = AUSGÄNGE/AUSGABEN/SERVOS but its not just a SERVO output"
#define TR_MENUCURVES                  TR_BW_COL("KURVEN", "Kurven")
#define TR_MIXES                       TR_BW_COL("MISCHER", "Mischer")
#define TR_MENU_GLOBAL_VARS            TR_BW_COL("GLOBALE VARIABLEN", "Globale Variablen")
#define TR_MENULOGICALSWITCHES         TR_BW_COL("LOGIKSCHALTER", "Logikschalter")
#define TR_MENUCUSTOMFUNC              TR_BW_COL("SPEZ.-FUNKTIONEN", "Spezial Funktionen")
#define TR_MENUCUSTOMSCRIPTS           TR_BW_COL("Lua-SKRIPTE", "Lua-Skripte")
#define TR_MENUTELEMETRY               TR_BW_COL("TELEMETRIE", "Telemetrie")
#define TR_MENUSPECIALFUNCS            TR_BW_COL("GLOBALE FUNKTIONEN", "Globale Funktionen")
#define TR_MENUTRAINER                 TR_BW_COL("LEHRER/SCHÜLER", "Lehrer/Schüler")
#define TR_HARDWARE                    TR_BW_COL("HARDWARE EINST. ", "Hardware Einstellungen")
#define TR_USER_INTERFACE              "Infozeile"
#define TR_SD_CARD                     TR_BW_COL("SD-KARTE", "SD-Karte")
#define TR_DEBUG                       "Debug"
#define TR_MENU_RADIO_SWITCHES         TR_BW_COL("SCHALTER-TEST", "Schalter-Test")
#define TR_MENUCALIBRATION             TR_BW_COL("KALIBRIERUNG.", "Kalibrierung")
#define TR_FUNCTION_SWITCHES           "Anpassbare Schalter"
// End Main menu

#define TR_MINUTE_SINGULAR             "Minute"
#define TR_MINUTE_PLURAL1              "Minuten"
#define TR_MINUTE_PLURAL2              "Minuten"

// NON ZERO TERMINATED STRINGS
#define TR_OFFON_1                     "AUS"
#define TR_OFFON_2                     "EIN"
#define TR_MMMINV_1                    "---"
#define TR_MMMINV_2                    "INV"
#define TR_VBEEPMODE_1                 "Stumm"
#define TR_VBEEPMODE_2                 "Alarm"
#define TR_VBEEPMODE_3                 "NoKey"
#define TR_VBEEPMODE_4                 "Alle"
#define TR_VBLMODE_1                   "AUS"
#define TR_VBLMODE_2                   "Taste"
#define TR_VBLMODE_3                   TR("Stks","Knüppel")
#define TR_VBLMODE_4                   "Beide"
#define TR_VBLMODE_5                   "EIN"
#define TR_TRNMODE_1                   "AUS"
#define TR_TRNMODE_2                   TR("+=","Addiere")
#define TR_TRNMODE_3                   TR(":=","Ersetze")
#define TR_TRNCHN_1                    "CH1"
#define TR_TRNCHN_2                    "CH2"
#define TR_TRNCHN_3                    "CH3"
#define TR_TRNCHN_4                    "CH4"
#define TR_AUX_SERIAL_MODES_1          "AUS"
#define TR_AUX_SERIAL_MODES_2          TR("Telem weiterl.", "Telemetrie weiterleiten")
#define TR_AUX_SERIAL_MODES_3          TR("Telemetrie In", "Telemetrie Eingang")
#define TR_AUX_SERIAL_MODES_4          TR("SBUS Trn Inv.","SBUS Trainer Inv.")
#define TR_AUX_SERIAL_MODES_5          "SBUS Eingang"
#define TR_AUX_SERIAL_MODES_6          "LUA"
#define TR_AUX_SERIAL_MODES_7          "CLI"
#define TR_AUX_SERIAL_MODES_8          "GPS"
#define TR_AUX_SERIAL_MODES_9          "Debug"
#define TR_AUX_SERIAL_MODES_10         "SpaceMouse"
#define TR_AUX_SERIAL_MODES_11         "Externes Modul"
#define TR_SWTYPES_1                   "Kein"
#define TR_SWTYPES_2                   "Taster"
#define TR_SWTYPES_3                   "2POS"
#define TR_SWTYPES_4                   "3POS"
#define TR_SWTYPES_5                   "Global"
#define TR_POTTYPES_1                  "Kein"
#define TR_POTTYPES_2                  "Poti"
#define TR_POTTYPES_3                  TR("Pot m. Ras","Poti mit Raste")
#define TR_POTTYPES_4                  "Schieber"
#define TR_POTTYPES_5                  TR("Multipos.","Multipos. Schalter")
#define TR_POTTYPES_6                  "Knüppel X"
#define TR_POTTYPES_7                  "Knüppel Y"
#define TR_POTTYPES_8                  "Schalter"
#define TR_VPERSISTENT_1               "AUS"
#define TR_VPERSISTENT_2               "Flugzeit"
#define TR_VPERSISTENT_3               TR("Manuell Rück","Manuell Rücksetzen")
#define TR_COUNTRY_CODES_1             TR("US","Amerika")
#define TR_COUNTRY_CODES_2             TR("JP","Japan")
#define TR_COUNTRY_CODES_3             TR("EU","Europa")
#define TR_USBMODES_1                  "Fragen"
#define TR_USBMODES_2                  TR("Joyst","Joystick")
#define TR_USBMODES_3                  TR("SDCard","Speicher")
#define TR_USBMODES_4                  TR("Serial","Seriell")
#define TR_JACK_MODES_1                "Popup"
#define TR_JACK_MODES_2                "Audio"
#define TR_JACK_MODES_3                "Trainer"

#define TR_SBUS_INVERSION_VALUES_1     "normal"
#define TR_SBUS_INVERSION_VALUES_2     "nicht inv."
#define TR_MULTI_CUSTOM                "Benutzer"
#define TR_VTRIMINC_1                  TR("Expo","Exponentiell")
#define TR_VTRIMINC_2                  TR("ExFein","Extrafein")
#define TR_VTRIMINC_3                  "Fein"
#define TR_VTRIMINC_4                  "Mittel"
#define TR_VTRIMINC_5                  "Grob"
#define TR_VDISPLAYTRIMS_1             "Nein"
#define TR_VDISPLAYTRIMS_2             "Kurz"
#define TR_VDISPLAYTRIMS_3             "Ja"  // Trimmwerte Keine, kurze Anzeigen, Ja
#define TR_VBEEPCOUNTDOWN_1            "Kein"
#define TR_VBEEPCOUNTDOWN_2            "Pieps"
#define TR_VBEEPCOUNTDOWN_3            "Stimme"
#define TR_VBEEPCOUNTDOWN_4            "Haptik"
#define TR_VBEEPCOUNTDOWN_5            TR("P & H","Pieps & Haptik")
#define TR_VBEEPCOUNTDOWN_6            TR("St & H","Stimme & Haptik")
#define TR_COUNTDOWNVALUES_1           "5s"
#define TR_COUNTDOWNVALUES_2           "10s"
#define TR_COUNTDOWNVALUES_3           "20s"
#define TR_COUNTDOWNVALUES_4           "30s"
#define TR_VVARIOCENTER_1              "Ton"
#define TR_VVARIOCENTER_2              "Ruhe"
#define TR_CURVE_TYPES_1               "Standard"
#define TR_CURVE_TYPES_2               "Eigene"

#define TR_ADCFILTERVALUES_1           "Global"
#define TR_ADCFILTERVALUES_2           "Aus"
#define TR_ADCFILTERVALUES_3           "Ein"

#define TR_VCURVETYPE_1                "Diff"
#define TR_VCURVETYPE_2                "Expo"
#define TR_VCURVETYPE_3                "Funk"
#define TR_VCURVETYPE_4                "Ind."
#define TR_VMLTPX_1                    "Addiere"
#define TR_VMLTPX_2                    "Multipl."
#define TR_VMLTPX_3                    "Ersetze"

#define TR_CSWTIMER                    "Takt"  // TIM = Takt = Taktgenerator
#define TR_CSWSTICKY                   "SRFF"  // Sticky = RS-Flip-Flop
#define TR_CSWSTAY                     "Puls"  // Edge = einstellbarer Impuls

#define TR_SF_TRAINER                  "Lehrer"
#define TR_SF_INST_TRIM                "Inst. Trim"
#define TR_SF_RESET                    "Rücksetz."
#define TR_SF_SET_TIMER                "Setze"
#define TR_SF_VOLUME                   "Lautstr."
#define TR_SF_FAILSAFE                 "SetFailsafe"
#define TR_SF_RANGE_CHECK              "RangeCheck"
#define TR_SF_MOD_BIND                 "ModuleBind"
#define TR_SF_RGBLEDS                  "RGB LED"

#define TR_SOUND                       "Spiel Töne"
#define TR_PLAY_TRACK                  TR("Datei abs.", "Datei abspielen")
#define TR_PLAY_VALUE                  TR("Sag Wert", "Wert ansagen")
#define TR_SF_HAPTIC                   "Haptik"
#define TR_SF_PLAY_SCRIPT              TR("Lua", "Lua-Skript")
#define TR_SF_BG_MUSIC                 "StartMusik"
#define TR_SF_BG_MUSIC_PAUSE           "StopMusik"
#define TR_SF_LOGS                     "SD-Aufz."
#define TR_ADJUST_GVAR                 "Ändere"
#define TR_SF_BACKLIGHT                "LCD Licht"
#define TR_SF_VARIO                    "Vario"
#define TR_SF_TEST                     "Test"
#define TR_SF_SAFETY                   TR("Übersch.", "Überschreibe")

#define TR_SF_SCREENSHOT               "Screenshot"
#define TR_SF_RACING_MODE              "RacingMode"
#define TR_SF_DISABLE_TOUCH            "Kein Touch"
#define TR_SF_DISABLE_AUDIO_AMP        "Audio Verst. Aus"
#define TR_SF_SET_SCREEN               TR_BW_COL("Seite anz.", "Hauptseite anzeigen")
#define TR_SF_PUSH_CUST_SWITCH         "LS setzen"
#define TR_SF_LCD_TO_VIDEO             "LCD zu Video"

#define TR_FSW_RESET_TELEM             TR("Telm","Telemetrie")
#define TR_FSW_RESET_TRIMS             "Trims"
#define TR_FSW_RESET_TIMERS_1          TR("Tmr1", "Timer1")
#define TR_FSW_RESET_TIMERS_2          TR("Tmr2", "Timer2")
#define TR_FSW_RESET_TIMERS_3          TR("Tmr3", "Timer3")


#define TR_VFSWRESET_1                 TR_FSW_RESET_TIMERS_1
#define TR_VFSWRESET_2                 TR_FSW_RESET_TIMERS_2
#define TR_VFSWRESET_3                 TR_FSW_RESET_TIMERS_3
#define TR_VFSWRESET_4                 "Alle"
#define TR_VFSWRESET_5                 TR_FSW_RESET_TELEM
#define TR_VFSWRESET_6                 TR_FSW_RESET_TRIMS

#define TR_FUNCSOUNDS_1                TR("Bp1","Piep1")
#define TR_FUNCSOUNDS_2                TR("Bp2","Piep2")
#define TR_FUNCSOUNDS_3                TR("Bp3","Piep3")
#define TR_FUNCSOUNDS_4                TR("Wrn1","Warnung1")
#define TR_FUNCSOUNDS_5                TR("Wrn2","Warnung2")
#define TR_FUNCSOUNDS_6                TR("Chee","Cheep")
#define TR_FUNCSOUNDS_7                TR("Rata","Ratata")
#define TR_FUNCSOUNDS_8                "Tick"
#define TR_FUNCSOUNDS_9                TR("Sirn","Sirene")
#define TR_FUNCSOUNDS_10               "Ring"
#define TR_FUNCSOUNDS_11               TR("SciF","SciFi")
#define TR_FUNCSOUNDS_12               TR("Robt","Robot")
#define TR_FUNCSOUNDS_13               TR("Chrp","Chirp")
#define TR_FUNCSOUNDS_14               "Tada"
#define TR_FUNCSOUNDS_15               TR("Crck","Crickt")
#define TR_FUNCSOUNDS_16               TR("Alrm","AlmClk")

#define LENGTH_UNIT_IMP                "ft"
#define SPEED_UNIT_IMP                 "mph"
#define LENGTH_UNIT_METR               "m"
#define SPEED_UNIT_METR                "kmh"
#define TR_VUNITSSYSTEM_1              TR("Metrik","Metrisch")
#define TR_VUNITSSYSTEM_2              TR("Imper.","Imperial")
#define TR_VTELEMUNIT_1                "-"
#define TR_VTELEMUNIT_2                "V"
#define TR_VTELEMUNIT_3                "A"
#define TR_VTELEMUNIT_4                "mA"
#define TR_VTELEMUNIT_5                "kts"
#define TR_VTELEMUNIT_6                "m/s"
#define TR_VTELEMUNIT_7                "f/s"
#define TR_VTELEMUNIT_8                "kmh"
#define TR_VTELEMUNIT_9                "mph"
#define TR_VTELEMUNIT_10               "m"
#define TR_VTELEMUNIT_11               "ft"
#define TR_VTELEMUNIT_12               "°C"
#define TR_VTELEMUNIT_13               "°F"
#define TR_VTELEMUNIT_14               "%"
#define TR_VTELEMUNIT_15               "mAh"
#define TR_VTELEMUNIT_16               "W"
#define TR_VTELEMUNIT_17               "mW"
#define TR_VTELEMUNIT_18               "dB"
#define TR_VTELEMUNIT_19               "rpm"
#define TR_VTELEMUNIT_20               "g"
#define TR_VTELEMUNIT_21               "°"
#define TR_VTELEMUNIT_22               "rad"
#define TR_VTELEMUNIT_23               "ml"
#define TR_VTELEMUNIT_24               "fOz"
#define TR_VTELEMUNIT_25               "mlm"
#define TR_VTELEMUNIT_26               "Hz"
#define TR_VTELEMUNIT_27               "ms"
#define TR_VTELEMUNIT_28               "us"
#define TR_VTELEMUNIT_29               "km"
#define TR_VTELEMUNIT_30               "dBm"

#define TR_VTELEMSCREENTYPE_1          "Kein"
#define TR_VTELEMSCREENTYPE_2          "Werte"
#define TR_VTELEMSCREENTYPE_3          "Balken"
#define TR_VTELEMSCREENTYPE_4          "Skript"
#define TR_GPSFORMAT_1                 "GMS"
#define TR_GPSFORMAT_2                 "NMEA"

#define TR_VSWASHTYPE_1                "---"
#define TR_VSWASHTYPE_2                "120"
#define TR_VSWASHTYPE_3                "120X"
#define TR_VSWASHTYPE_4                "140"
#define TR_VSWASHTYPE_5                "90"

#define TR_STICK_NAMES0                "Sei"
#define TR_STICK_NAMES1                "Höh"
#define TR_STICK_NAMES2                "Gas"
#define TR_STICK_NAMES3                "Que"
#define TR_SURFACE_NAMES0              "Str"
#define TR_SURFACE_NAMES1              "Gas"

#define TR_ON_ONE_SWITCHES_1           "ON"
#define TR_ON_ONE_SWITCHES_2           "One"

#define TR_HATSMODE                    "Joystick Modus"
#define TR_HATSOPT_1                   "nur Trimmer"
#define TR_HATSOPT_2                   "nur Tasten"
#define TR_HATSOPT_3                   "Umschaltbar"
#define TR_HATSOPT_4                   "Global"
#define TR_HATSMODE_TRIMS              "Joystick Modus: Trimmer"
#define TR_HATSMODE_KEYS               "Joystick Modus: Tasten"
#define TR_HATSMODE_KEYS_HELP          "Linke Seite:\n"\
                                       " Rechts = MDL\n"\
                                       " Oben = SYS\n"\
                                       " Unten = TELE\n"\
                                       "\n"\
                                       "Rechte Seite:\n"\
                                       " Links = PAGE<\n"\
                                       " Rechts = PAGE>\n"\
                                       " Oben = PREV/INC\n"\
                                       " Unten = NEXT/DEC"

#define TR_ROTARY_ENC_OPT_1            "Normal"
#define TR_ROTARY_ENC_OPT_2            "Invertiert"
#define TR_ROTARY_ENC_OPT_3            "V-I H-N"
#define TR_ROTARY_ENC_OPT_4            "V-I H-A"
#define TR_ROTARY_ENC_OPT_5            "V-N E-I"
#define TR_IMU_VSRCRAW_1               "NeigX"
#define TR_IMU_VSRCRAW_2               "NeigY"

#define TR_CYC_VSRCRAW_1               "CYC1"
#define TR_CYC_VSRCRAW_2               "CYC2"
#define TR_CYC_VSRCRAW_3               "CYC3"

#define TR_SRC_BATT                    "Batt"
#define TR_SRC_TIME                    "Zeit"
#define TR_SRC_GPS                     "GPS"
#define TR_SRC_LIGHT                   "Umgebungslicht"
#define TR_SRC_TIMER                   TR("Tmr", "Timer")

#define TR_VTMRMODES_1                 "AUS"
#define TR_VTMRMODES_2                 "EIN"
#define TR_VTMRMODES_3                 "Strt"
#define TR_VTMRMODES_4                 "GSs"
#define TR_VTMRMODES_5                 "GS%"
#define TR_VTMRMODES_6                 "GSt"
#define TR_VTRAINER_MASTER_OFF         "AUS"
#define TR_VTRAINER_MASTER_JACK        "Lehrer/Buchse"
#define TR_VTRAINER_SLAVE_JACK         "Schüler/Buchse"
#define TR_VTRAINER_MASTER_SBUS_MODULE "Lehrer/SBUS Modul"
#define TR_VTRAINER_MASTER_CPPM_MODULE "Lehrer/CPPM Modul"
#define TR_VTRAINER_MASTER_BATTERY     "Lehrer/Serial"
#define TR_VTRAINER_BLUETOOTH_1        TR("Lehrer/BT","Lehrer/Bluetooth")
#define TR_VTRAINER_BLUETOOTH_2        TR("Schüler/BT","Schüler/Bluetooth")
#define TR_VTRAINER_MULTI              "Lehrer/Multi"
#define TR_VTRAINER_CRSF               "Lehrer/CRSF"
#define TR_VFAILSAFE_1                 "Kein Failsafe"
#define TR_VFAILSAFE_2                 "Halte Pos."
#define TR_VFAILSAFE_3                 "Kanäle"
#define TR_VFAILSAFE_4                 "Kein Signal"
#define TR_VFAILSAFE_5                 "Empfänger"
#define TR_VSENSORTYPES_1              "Sensor"
#define TR_VSENSORTYPES_2              "Berechnung"
#define TR_VFORMULAS_1                 "Addieren"
#define TR_VFORMULAS_2                 "Mittelwert"
#define TR_VFORMULAS_3                 "Min"
#define TR_VFORMULAS_4                 "Max"
#define TR_VFORMULAS_5                 "Multiplizier"
#define TR_VFORMULAS_6                 "Gesamt"
#define TR_VFORMULAS_7                 "Zelle"
#define TR_VFORMULAS_8                 "Verbrauch"
#define TR_VFORMULAS_9                 "Distanz"
#define TR_VPREC_1                     "0.--"
#define TR_VPREC_2                     "0.0 " //align with en.h also uses a space to keep the same width as 0.00
#define TR_VPREC_3                     "0.00"
#define TR_VCELLINDEX_1                "Niedrigst"
#define TR_VCELLINDEX_2                "1. Zelle"
#define TR_VCELLINDEX_3                "2. Zelle"
#define TR_VCELLINDEX_4                "3. Zelle"
#define TR_VCELLINDEX_5                "4. Zelle"
#define TR_VCELLINDEX_6                "5. Zelle"
#define TR_VCELLINDEX_7                "6. Zelle"
#define TR_VCELLINDEX_8                "7. Zelle"
#define TR_VCELLINDEX_9                "8. Zelle"
#define TR_VCELLINDEX_10               "Höchster"
#define TR_VCELLINDEX_11               "Differenz"
#define TR_SUBTRIMMODES_1              CHAR_DELTA" (nur Mitte)"
#define TR_SUBTRIMMODES_2              "= (symmetrisch)"
#define TR_TIMER_DIR_1                 TR("Rückw.", "Rückwärts")
#define TR_TIMER_DIR_2                 TR("Vorwä.", "Vorwärts")

#define TR_FONT_SIZES_1                "STD"
#define TR_FONT_SIZES_2                "FETT"
#define TR_FONT_SIZES_3                "XXS"
#define TR_FONT_SIZES_4                "XS"
#define TR_FONT_SIZES_5                "L"
#define TR_FONT_SIZES_6                "XL"
#define TR_FONT_SIZES_7                "XXL"
#define TR_FONT_SIZES_8                "LXL"

#define TR_ENTER                       "[ENTER]"
#define TR_OK                          TR_BW_COL(TR("\010\010\010[OK]", "\010\010\010\010\010[OK]"), "Ok")
#define TR_EXIT                        TR_BW_COL("EXIT", "RTN")

#define TR_YES                         "Ja"
#define TR_NO                          "Nein"
#define TR_DELETEMODEL                 "Modell löschen?"
#define TR_COPYINGMODEL                "Kopiere Modell"
#define TR_MOVINGMODEL                 "Verschiebe Modell"
#define TR_LOADINGMODEL                "Lade Modell..."
#define TR_UNLABELEDMODEL              "Kein Label"
#define TR_NAME                        "Name"
#define TR_MODELNAME                   "Modellname"
#define TR_PHASENAME                   "Phase-Name"
#define TR_MIXNAME                     "Mix-Name"
#define TR_INPUTNAME                   TR("GEBER", "Gebername")
#define TR_EXPONAME                    TR("Name", "Zeilenname")
#define TR_BITMAP                      "Modellfoto"
#define TR_NO_PICTURE                  "Kein Foto"
#define TR_TIMER                       "Timer"
#define TR_NO_TIMERS                   "Keine Timer"
#define TR_START                       "Start"
#define TR_NEXT                        "Weiter"
#define TR_ELIMITS                     TR("Erw. Limit", "Erw. Wege auf 150%")
#define TR_ETRIMS                      TR("Erw. Trims", "Erw. Trim  auf 100%")
#define TR_TRIMINC                     TR("Trimm-Ink.", "Trimmschritte")
#define TR_DISPLAY_TRIMS               TR("Trimm-Anz.", "Trimmwerte anzeigen")
#define TR_TTRACE                      TR("Gasquelle", "Gas-Timerquelle")
#define TR_TTRIM                       TR("Gastrim", "Gas-Leerlauftrim")
#define TR_TTRIM_SW                    TR("T-Trim-Sw", "Trimmschalter")
#define TR_BEEPCTR                     TR("MittePieps", "Pieps in Mittelstellung")
#define TR_USE_GLOBAL_FUNCS            TR("Glob. Funkt.", "Globale Funkt verw.")
#define TR_PROTOCOL                    TR("Protok.", "Protokoll")
#define TR_PPMFRAME                    "PPM-Frame"
#define TR_REFRESHRATE                 TR("Refresh", "Refresh Rate")
#define TR_WARN_BATTVOLTAGE            TR("Ausg. ist VBAT: ", "Warnung: Ausg.pegel ist VBAT: ")
#define TR_WARN_5VOLTS                 "Warnung: Ausgangspegel ist 5 Volt"
#define TR_MS                          "ms"
#define TR_SWITCH                      TR("Schalt.", "Schalter")
#define TR_FS_COLOR_LIST_1             "Benutzerdef."
#define TR_FS_COLOR_LIST_2             "Aus"
#define TR_FS_COLOR_LIST_3             "Weiß"
#define TR_FS_COLOR_LIST_4             "Rot"
#define TR_FS_COLOR_LIST_5             "Grün"
#define TR_FS_COLOR_LIST_6             "Gelb"
#define TR_FS_COLOR_LIST_7             "Orange"
#define TR_FS_COLOR_LIST_8             "Blau"
#define TR_FS_COLOR_LIST_9             "Pink"
#define TR_GROUP                       "Gruppe"
#define TR_GROUP_ALWAYS_ON             "Immer an"
#define TR_LUA_OVERRIDE                "Lua darf überschr."
#define TR_GROUPS                      "Immer an Gruppen"
#define TR_LAST                        "Letzte"
#define TR_MORE_INFO                   "Mehr Info"
#define TR_SWITCH_TYPE                 "Typ"
#define TR_SWITCH_STARTUP              "Bei Start"
#define TR_SWITCH_GROUP                "Gruppe"
#define TR_SF_SWITCH                   "Auslöser"
#define TR_TRIMS                       "Trimmer"
#define TR_FADEIN                      TR("Langs. Ein", "Langsam Ein")
#define TR_FADEOUT                     TR("Langs. Aus", "Langsam Aus")
#define TR_DEFAULT                     "(Normal)"
#define TR_CHECKTRIMS                  TR_BW_COL("\006Prüfe\012Trimmung", "Prüfe Flugphasen-Trimmung")
#define TR_SWASHTYPE                   TR("Typ Taumelsch", "Typ  Taumelscheibe")
#define TR_COLLECTIVE                  TR("Kollekt. Pitch", "Kollekt. Pitch Quelle")
#define TR_AILERON                     "Roll Quelle"
#define TR_ELEVATOR                    "Nick Quelle"
#define TR_SWASHRING                   TR("Ring   Begrenz", "Ring Begrenzung")
#define TR_MODE                        "Modus"
#define TR_LEFT_STICK                  "Links"
#define TR_SUBTYPE                     "Subtype"
#define TR_NOFREEEXPO                  "Expos voll!"
#define TR_NOFREEMIXER                 "Mischer voll!"
#define TR_TOO_MANY_MODELS     "Model limit reached"
#define TR_SOURCE                      "Quelle"
#define TR_WEIGHT                      "Gewicht"
#define TR_SIDE                        "Seite"
#define TR_OFFSET                      "Offset"
#define TR_TRIM                        "Trim"
#define TR_CURVE                       "Kurve"
#define TR_FLMODE                      TR("Phase", "Phasen")
#define TR_MIXWARNING                  "Warnung"
#define TR_OFF                         "AUS"
#define TR_ANTENNA                     "Antenne"
#define TR_NO_INFORMATION              TR("Keine Info", "Keine Information")
#define TR_MULTPX                      "Wirkung"
#define TR_DELAYDOWN                   "Verz.Ab"
#define TR_DELAYUP                     "Verz.Auf"
#define TR_SLOWDOWN                    "Langs.Ab"
#define TR_SLOWUP                      "Langs.Auf"
#define TR_CV                          "KV"
#define TR_GV                          TR("G", "GV")
#define TR_RANGE                       TR("Bereich", "Variobereich m/s")
#define TR_CENTER                      TR("Mitte", "Variomitte     m/s")
#define TR_ALARM                       "Alarme"
#define TR_BLADES                      TR("Prop", "Prop-Blätter")
#define TR_SCREEN                      "Seite: "
#define TR_SOUND_LABEL                 "Töne"
#define TR_LENGTH                      "Dauer"
#define TR_BEEP_LENGTH                 "Piep-Länge"
#define TR_BEEP_PITCH                  "Piep-Freq +/-"
#define TR_HAPTIC_LABEL                "Haptik"
#define TR_STRENGTH                    "Stärke"
#define TR_IMU_LABEL                   "IMU"
#define TR_IMU_OFFSET                  "Offset"
#define TR_IMU_MAX                     "Max"
#define TR_CONTRAST                    "LCD-Kontrast"
#define TR_ALARMS_LABEL                "Alarme"
#define TR_BATTERY_RANGE               TR("Akku Bereich", "Akku Spannungsbereich") // Symbol Akku Ladezustand
#define TR_BATTERYCHARGING             "Lädt..."
#define TR_BATTERYFULL                 "Akku voll"
#define TR_BATTERYNONE                 "Keine!"
#define TR_BATTERYWARNING              TR("Akku Warnung", "Akkuspannungswarnung")
#define TR_INACTIVITYALARM             TR("Inaktivität", "Inaktivität nach")
#define TR_MEMORYWARNING               "Speicher voll"
#define TR_ALARMWARNING                TR("Alle Töne aus?", "Alle Töne ganz aus?")
#define TR_RSSI_SHUTDOWN_ALARM         TR("RSSI b. Aussch.", "Prüfe RSSI bei Ausschalten")
#define TR_TRAINER_SHUTDOWN_ALARM      TR("Trainer b. Aussch.", "Prüfe Trainer bei Ausschalten")
#define TR_MODEL_STILL_POWERED         "Modell noch aktiv!"
#define TR_TRAINER_STILL_CONNECTED     "Schüler noch verbunden"
#define TR_USB_STILL_CONNECTED         "USB noch verbunden"
#define TR_MODEL_SHUTDOWN              "Herunterfahren?"
#define TR_PRESS_ENTER_TO_CONFIRM      "Drücke [ENTER] zum Bestätigen"
#define TR_THROTTLE_LABEL              "Gas-Kontrolle"
#define TR_THROTTLE_START              "Gas Start"
#define TR_THROTTLEREVERSE             TR("Gas invers", "Vollgas hinten?") //Änderung wg TH9x, Taranis
#define TR_MINUTEBEEP                  TR("Min-Alarm", "Minuten-Alarm")
#define TR_BEEPCOUNTDOWN               "Countdown"
#define TR_PERSISTENT                  TR("Permanent", "Permanent")
#define TR_BACKLIGHT_LABEL             "Bildschirm"
#define TR_GHOST_MENU_LABEL            "GHOST MENÜ"
#define TR_STATUS                      "Status"
#define TR_BLONBRIGHTNESS              "An-Helligkeit"
#define TR_BLOFFBRIGHTNESS             "Aus-Helligkeit"
#define TR_KEYS_BACKLIGHT              "Tastenbeleucht."
#define TR_BLCOLOR                     "Farbe"
#define TR_ONE_LOG_PER_DAY             "One log per day"
#define TR_KEY_LOCK_FMT                "Key lock (%s+%s hold)"
#define TR_KEYS_LOCKED                 "Keys locked"
#define TR_KEYS_LOCKED_FMT             "Keys locked (%s+%s)"
#define TR_KEYS_UNLOCKED               "Keys unlocked"
#define TR_SPLASHSCREEN                TR("Startbild Ein", "Startbild Anzeigedauer")
#define TR_PLAY_HELLO                  "Startton abspielen"
#define TR_PWR_ON_DELAY                TR("PWR EIN Verzög.", "Einschaltverzögerung")
#define TR_PWR_OFF_DELAY               TR("PWR AUS Verzög.", "Ausschaltverzögerung")
#define TR_PWR_AUTO_OFF                TR("Autom. Ausschalten","Automatisch Ausschalten")
#define TR_PWR_ON_OFF_HAPTIC           TR("PWR EIN/AUS Haptik","Power EIN/AUS Haptik")
#define TR_THROTTLE_WARNING            TR("Gasalarm", "Gas Alarm")
#define TR_CUSTOM_THROTTLE_WARNING     TR("Eigene Pos.", "Eigene Position")
#define TR_CUSTOM_THROTTLE_WARNING_VAL TR("Pos. %", "Position %")
#define TR_SWITCHWARNING               TR("Sch. Alarm", "Schalter-Alarm")
#define TR_POTWARNINGSTATE             "Potis & Schieber"
#define TR_POTWARNING                  TR("Potiwarn.", "Poti-Warnung")
#define TR_TIMEZONE                    TR("Zeitzone", "GPS-Zeitzone +/-Std")
#define TR_ADJUST_RTC                  TR("GPSzeit setzen", "Uhrzeit per GPS setzen")
#define TR_GPS                         "GPS"
#define TR_DEF_CHAN_ORD                TR("Std.Kanal Folge", "Std. Kanal Reihenfolge")
#define TR_STICKS                      "Knüppel"
#define TR_POTS                        "Potis"
#define TR_SWITCHES                    "Schalter"
#define TR_SWITCHES_DELAY              TR("Sw. Mitte Delay", "Schaltermitte Verzögerung")   //Schalter Mitten verzögern Anpassung
#define TR_SLAVE                       TR("Schüler PPM1-16", "Schüler PPM1-16 als Ausgang")
#define TR_MODESRC                     "Modus\003%  Quelle"
#define TR_MULTIPLIER                  "Multiplik."
#define TR_CAL                         "Kal."
#define TR_CALIBRATION                 BUTTON("Kalibrierung")
#define TR_VTRIM                       "Trim - +"
#define TR_CALIB_DONE                  "Kalibrierung fertig"
#define TR_MENUTOSTART                 TR_ENTER " Zum START"
#define TR_MENUWHENDONE                TR_ENTER " wenn fertig"
#define TR_AXISDIR                     TR_BW_COL("ACHSEN RICHTUNG", "Achsen Richtung")
#define TR_MENUAXISDIR                 "[ENTER LANG] "  TR_AXISDIR
#define TR_SETMIDPOINT                 TR_BW_COL(TR_SFC_AIR("SCHIEBER AUF MITTE", TR("KNÜPPEL AUF MITTE", "ZENTRIERE KNÜPPEL/SCHIEBER")), "Knüppel/Schieber zentrieren")
#define TR_MOVESTICKSPOTS              TR_BW_COL(TR_SFC_AIR("BEWEGE LENK/GAS/POTI/KNÜPPEL", "BEWEGE KNÜPPEL/SCHIEBER"), "Knüppel/Schieber bewegen")
#define TR_NODATA                      "Keine Daten"
#define TR_US                          "us"
#define TR_HZ                          "Hz"
#define TR_TMIXMAXMS                   "Tmix max"
#define TR_FREE_STACK                  "Freier Stack"
#define TR_INT_GPS_LABEL               "Internes GPS"
#define TR_HEARTBEAT_LABEL             "Heartbeat"
#define TR_LUA_SCRIPTS_LABEL           "Lua-Skripte"
#define TR_FREE_MEM_LABEL              "Freier Speicher"
#define TR_DURATION_MS                 TR("[D]","Dauer(ms): ")
#define TR_INTERVAL_MS                 TR("[I]","Intervall(ms): ")
#define TR_MEM_USED_SCRIPT             "Skript(B): "
#define TR_MEM_USED_WIDGET             "Widget(B): "
#define TR_MEM_USED_EXTRA              "Extra(B): "
#define TR_STACK_MIX                   "Mix: "
#define TR_STACK_AUDIO                 "Audio: "
#define TR_GPS_FIX_YES                 "Fix: Ja"
#define TR_GPS_FIX_NO                  "Fix: Nein"
#define TR_GPS_SATS                    "Sats: "
#define TR_GPS_HDOP                    "Hdop: "
#define TR_STACK_MENU                  "Menü: "
#define TR_TIMER_LABEL                 "Timer"
#define TR_THROTTLE_PERCENT_LABEL      "Gas %"
#define TR_BATT_LABEL                  "Batterie"
#define TR_SESSION                     "Sitzung"
#define TR_MENUTORESET                 TR_ENTER " für Reset"
#define TR_PPM_TRAINER                 "TR"
#define TR_CH                          "CH"
#define TR_MODEL                       TR_BW_COL("MODELL", "Modell")
#define TR_FM                          TR_SFC_AIR("DM", "FP")
#define TR_EEPROMLOWMEM                "EEPROM voll"
#define TR_PRESS_ANY_KEY_TO_SKIP       "Beliebige Taste drücken"
#define TR_THROTTLE_NOT_IDLE           "Gas nicht Null!"
#define TR_ALARMSDISABLED              "Alarme ausgeschaltet"
#define TR_PRESSANYKEY                 "Taste drücken"
#define TR_BADEEPROMDATA               "EEPROM ungültig"
#define TR_BAD_RADIO_DATA              "Fehlende oder fehlerhafte Daten"
#define TR_RADIO_DATA_RECOVERED        TR3("Backup Senderdaten verw.","Backup Senderdaten verw.","Backup Senderdaten wurden verwendet")
#define TR_RADIO_DATA_UNRECOVERABLE    TR3("Senderdaten ungültig","Senderdaten ungültig", "Senderdaten ungültig")
#define TR_EEPROMFORMATTING            "EEPROM Initialisieren"
#define TR_STORAGE_FORMAT              "Speicher Vorbereiten"
#define TR_EEPROMOVERFLOW              "EEPROM Überlauf"
#define TR_RADIO_SETUP                 TR_BW_COL(TR("SENDER-EINSTELLEN", "SENDER-GRUNDEINSTELLUNGEN"), "Sender Grundeinstellungen")
#define TR_MENUVERSION                 TR_BW_COL("VERSION", "Version")
#define TR_MENU_RADIO_ANALOGS          "Geber-Test"
#define TR_MENU_RADIO_ANALOGS_CALIB    TR_BW_COL("GEBER KALIBRIERT", "Geber kalibriert")
#define TR_MENU_RADIO_ANALOGS_RAWLOWFPS "Rohwerte (5 Hz)"
#define TR_MENU_FSWITCH                TR_BW_COL("ANPASSBARE SCHALTER", "Anpassbasre Schalter")
#define TR_TRIMS2OFFSETS               TR_BW_COL("\006Trims => Subtrims", "Trims => Subtrims")
#define TR_CHANNELS2FAILSAFE           "Channels=>Failsafe"
#define TR_CHANNEL2FAILSAFE            "Channel=>Failsafe"
#define TR_MENUMODELSEL                TR_BW_COL(TR("MODELLE", "MODELL WÄHLEN"), "Modell wählen")
#define TR_MENU_MODEL_SETUP            TR_BW_COL("MODELL-EINSTELLUNG", "Modell Einstellungen")
#define TR_MENUCURVE                   TR_BW_COL("KURVE", "Kurve")
#define TR_MENULOGICALSWITCH           TR_BW_COL("LOGIKSCHALTER", "Logikschalter")
#define TR_MENUSTAT                    TR_BW_COL("STAT", "Statistik")
#define TR_MENUDEBUG                   TR_BW_COL("DEBUG", "Debug")
#define TR_MONITOR_CHANNELS            TR_BW_COL("KANAL+MISCHER MONITOR %d-%d", "Kanal+Mischer Monitor %d-%d")
#define TR_MONITOR_OUTPUT_DESC         "Kanäle"
#define TR_MONITOR_MIXER_DESC          "Mischer"
#define TR_RECEIVER_NUM                TR("Empf Nr.", "Empfänger Nummer")
#define TR_RECEIVER                    "Empfänger"
#define TR_MULTI_RFTUNE                TR("RF Freq.", "RF Freq. Feintuning")
#define TR_MULTI_RFPOWER               TR("RF Power", "RF Leistung")
#define TR_MULTI_WBUS                  TR("Output", "Ausgang")
#define TR_MULTI_TELEMETRY             "Telemetrie"
#define TR_MULTI_VIDFREQ               TR("Vid. Freq.", "Video Frequenz")
#define TR_RF_POWER                    "RF Leistung"
#define TR_MULTI_FIXEDID               TR("FesteID", "Feste ID")
#define TR_MULTI_OPTION                TR("Option", "Optionswert")
#define TR_MULTI_AUTOBIND              TR("Bind Ka.","Bindung an Kanal")
#define TR_DISABLE_CH_MAP              TR("No Ch. map", "Deaktiviere Ch. map")
#define TR_DSMP_ENABLE_AETR            TR("AETR ein", "AETR-Folge nutzen")
#define TR_DISABLE_TELEM               TR("Telem aus", "Deaktiviere Telem.")
#define TR_MULTI_LOWPOWER              TR("Min. Leist.", "Reduzierte Leistung")
#define TR_MULTI_LNA_DISABLE           "LNA deaktiv."
#define TR_MODULE_TELEMETRY            TR("S.Port", "S.Port link")
#define TR_MODULE_TELEM_ON             TR("EIN", "Aktiviert")
#define TR_DISABLE_INTERNAL            TR("Deaktiv. int. RF", "Deaktiviere int. RF")
#define TR_MODULE_NO_SERIAL_MODE       TR("!serial mode", "Nicht im Serial-Modus")
#define TR_MODULE_NO_INPUT             TR("Kein input", "Kein serielles Signal")
#define TR_MODULE_NO_TELEMETRY         TR3("Keine Telem.", "Keine MULTI_TELEMETRIE", "Keine Telemetrie (aktiviere MULTI_TELEMETRIE)")
#define TR_MODULE_WAITFORBIND          "Warten auf Bindung"
#define TR_MODULE_BINDING              "Binde"
#define TR_MODULE_UPGRADE_ALERT        TR3("Upg. nötig", "Modul Upgrade nötig", "Modul\nUpgrade nötig")
#define TR_MODULE_UPGRADE              TR("Upg. empf.", "Modul Upgrade empf.")
#define TR_REBIND                      "Neu binden nötig"
#define TR_REG_OK                      "Registration ok"
#define TR_BIND_OK                     "Binden erfolgreich"
#define TR_BINDING_CH1_8_TELEM_ON      "Ch1-8 Telem AN"
#define TR_BINDING_CH1_8_TELEM_OFF     "Ch1-8 Telem AUS"
#define TR_BINDING_CH9_16_TELEM_ON     "Ch9-16 Telem AN"
#define TR_BINDING_CH9_16_TELEM_OFF    "Ch9-16 Telem AUS"
#define TR_PROTOCOL_INVALID            TR("Prot. invalid", "Protokoll ungültig")
#define TR_MODULE_STATUS               TR("Status", "Modul Status")
#define TR_MODULE_SYNC                 TR("Sync", "Proto Sync Status")
#define TR_MULTI_SERVOFREQ             TR("Servo Rate", "Servo Update Rate")
#define TR_MULTI_MAX_THROW             TR("Max. Weg", "Aktiviere Max. Weg")
#define TR_MULTI_RFCHAN                TR("RF Channel", "Wähle RF Kanal")
#define TR_AFHDS3_RX_FREQ              TR("RX Freq.", "Empfänger-Freq.")
#define TR_AFHDS3_ONE_TO_ONE_TELEMETRY TR("Unicast/Tel.", "Unicast/Telemetrie")
#define TR_AFHDS3_ONE_TO_MANY          "Multicast"
#define TR_AFHDS3_ACTUAL_POWER         TR("Ist-Leist.", "Aktuelle Leistung")
#define TR_AFHDS3_POWER_SOURCE         TR("Versorg.", "Spannungsquelle")
#define TR_IBUS2_SENSORS_MODE_ONLY     "Only in the iBUS2 mode can the sensors be set."
#define TR_FLYSKY_TELEMETRY            TR("FlySky RSSI #", "Verwende FlySky RSSI (skalierungsfrei)")
#define TR_GPS_COORDS_FORMAT           TR("GPS-Koord.", "GPS-Koordinaten-Format")
#define TR_VARIO                       TR("Vario", "Variometer")
#define TR_PITCH_AT_ZERO               "Töne sinken"
#define TR_PITCH_AT_MAX                "Töne steigen"
#define TR_REPEAT_AT_ZERO              "Wiederholrate"
#define TR_BATT_CALIB                  TR("AkkuSpgwert", "Akku Kalibrierung")
#define TR_CURRENT_CALIB               "Strom abgl."
#define TR_VOLTAGE                     TR("Spg", "Spannungsquelle")  //9XR-Pro
#define TR_SELECT_MODEL                "Modell auswählen"
#define TR_MANAGE_MODELS               "MODELL MANAGER"
#define TR_MODELS                      "Modelle"
#define TR_SELECT_MODE                 "Wähle Mode"
#define TR_CREATE_MODEL                TR("Neues Modell" , "Neues Modell erstellen")
#define TR_FAVORITE_LABEL              "Favoriten"
#define TR_MODELS_MOVED                "Unbenutzte Modelle werden verschoben nach"
#define TR_NEW_MODEL                   "Neues Modell"
#define TR_INVALID_MODEL               "ungültiges Modell"
#define TR_EDIT_LABELS                 "Label ändern"
#define TR_LABEL_MODEL                 "Label zuordnen"
#define TR_MOVE_UP                     "Verschiebe nach oben"
#define TR_MOVE_DOWN                   "Verschiebe nach unten"
#define TR_ENTER_LABEL                 "Label eingeben"
#define TR_LABEL                       "Label"
#define TR_LABELS                      "Labels"
#define TR_CURRENT_MODEL               "aktuell"
#define TR_ACTIVE                      "Aktiv"
#define TR_NEW                         "Neu"
#define TR_NEW_LABEL                   "Neues Label"
#define TR_RENAME_LABEL                "Label umbenennen"
#define TR_DELETE_LABEL                "Label löschen"
#define TR_DUPLICATE_MODEL             "Modell duplizieren"
#define TR_COPY_MODEL                  "Modell kopieren"
#define TR_MOVE_MODEL                  "Modell verschieben"
#define TR_BACKUP_MODEL                "Modell auf SD-Karte"  //9XR-Pro
#define TR_DELETE_MODEL                "Modell löschen" // TODO merged into DELETEMODEL?
#define TR_RESTORE_MODEL               TR("Modell wiederher.", "Modell wiederherstellen")
#define TR_DELETE_ERROR                "Fehler beim\nLöschen"
#define TR_SDCARD_ERROR                "SD-Kartenfehler"
#define TR_SDCARD                      "SD-Karte"
#define TR_NO_FILES_ON_SD              "Keine Dateien auf SD!"
#define TR_NO_SDCARD                   "Keine SD-Karte"
#define TR_WAITING_FOR_RX              "Warten auf RX..."
#define TR_WAITING_FOR_TX              "Warten auf TX..."
#define TR_WAITING_FOR_MODULE          TR("Warten Modul", "Warten auf Modul...")
#define TR_NO_TOOLS                    "Keine Tools vorhanden"
#define TR_NORMAL                      "Normal"
#define TR_NOT_INVERTED                TR("Nicht inv", "Nicht invertiert")
#define TR_NOT_CONNECTED               TR("!Verbunden", "Nicht Verbunden")
#define TR_CONNECTED                   "Verbunden"
#define TR_FLEX_915                    "Flex 915MHz"
#define TR_FLEX_868                    "Flex 868MHz"
#define TR_16CH_WITHOUT_TELEMETRY      TR("16CH ohne Telem.", "16CH ohne Telemetrie")
#define TR_16CH_WITH_TELEMETRY         TR("16CH mit Telem.", "16CH mit Telemetrie")
#define TR_8CH_WITH_TELEMETRY          TR("8CH mit Telem.", "8CH mit Telemetrie")
#define TR_EXT_ANTENNA                 "Ext. Antenne"
#define TR_PIN                         "Pin"
#define TR_UPDATE_RX_OPTIONS           "Update RX Optionen?"
#define TR_UPDATE_TX_OPTIONS           "Update TX Optionen?"
#define TR_MODULES_RX_VERSION          BUTTON("Modul / RX version")
#define TR_SHOW_MIXER_MONITORS         "Zeige Mischermonitor"
#define TR_MENU_MODULES_RX_VERSION     "MODUL / RX VERSION"
#define TR_MENU_FIRM_OPTIONS           TR_BW_COL("FIRMWARE OPTIONEN", "Firmware Optionen")
#define TR_IMU                         "IMU"
#define TR_STICKS_POTS_SLIDERS         "Knüppel/Poti/Schieber"
#define TR_PWM_STICKS_POTS_SLIDERS     "PWM Knüppel/Poti/Schieber"
#define TR_RF_PROTOCOL                 "RF Protokoll"
#define TR_MODULE_OPTIONS              "Modul Optionen"
#define TR_POWER                       "Leistung"
#define TR_NO_TX_OPTIONS               "keine TX Optionen"
#define TR_RTC_BATT                    "RTC Batt"
#define TR_POWER_METER_EXT             "Leistungsmesser (EXT)"
#define TR_POWER_METER_INT             "Leistungsmesser (INT)"
#define TR_SPECTRUM_ANALYSER_EXT       "Spektrum (EXT)"
#define TR_SPECTRUM_ANALYSER_INT       "Spektrum (INT)"
#define TR_GHOST_MODULE_CONFIG         "Ghost Konfig."
#define TR_GPS_MODEL_LOCATOR           "GPS Modell-Finder"
#define TR_REFRESH                     "Refresh"
#define TR_SDCARD_FULL                 "SD-Karte voll"
#define TR_SDCARD_FULL_EXT              TR_BW_COL(TR_SDCARD_FULL "\036Logs und " LCDW_128_LINEBREAK "Screenshots deaktiviert", TR_SDCARD_FULL "\nLogs und Screenshots deaktiviert")
#define TR_NEEDS_FILE                  "Datei benötigt"
#define TR_EXT_MULTI_SPEC              "opentx-inv"
#define TR_INT_MULTI_SPEC              "stm-opentx-noinv"
#define TR_INCOMPATIBLE                "Nicht kompatibel"
#define TR_WARNING                     "WARNUNG"
#define TR_STORAGE_WARNING             "SPEICHER"
#define TR_THROTTLE_UPPERCASE          "GAS"
#define TR_ALARMSWARN                  "ALARM"
#define TR_SWITCHWARN                  "SCHALTER"
#define TR_FAILSAFEWARN                "FAILSAFE"
#define TR_TEST_WARNING                TR("TESTING", "TEST BUILD")
#define TR_TEST_NOTSAFE                "Nur für Testzwecke!"
#define TR_WRONG_SDCARDVERSION         TR("Erw. Version: ","Erwartete Version: ")
#define TR_WARN_RTC_BATTERY_LOW        "RTC Batterie schwach"
#define TR_WARN_MULTI_LOWPOWER         "Reduzierte Leistung"
#define TR_BATTERY                     "AKKU"
#define TR_WRONG_PCBREV                "Falsche PCB erkannt"
#define TR_EMERGENCY_MODE              "NOTFALL MODUS"
#define TR_NO_FAILSAFE                 TR("Failsafe not set", "Failsafe nicht programmiert")
#define TR_KEYSTUCK                    "Taste klemmt"  //Key stuck=Taste klemmt
#define TR_VOLUME                      "Lautstärke"
#define TR_LCD                         "Bildschirm"
#define TR_BRIGHTNESS                  "Helligkeit"
#define TR_CPU_TEMP                    "CPU-Temp.\016>"
#define TR_COPROC                      "CoProz."
#define TR_COPROC_TEMP                 "MB Temp. \016>"
#define TR_TTL_WARNING                 "Warnung: An den TX/RX Pins dürfen 3.3V nicht überschritten werden!"
#define TR_FUNC                        "Funktion"
#define TR_V1                          "V1"
#define TR_V2                          "V2"
#define TR_DURATION                    "Dauer"
#define TR_DELAY                       "Verzögerung"
#define TR_NO_SOUNDS_ON_SD             "Keine Töne auf SD"
#define TR_NO_MODELS_ON_SD             "Keine Modelle auf SD"
#define TR_NO_BITMAPS_ON_SD            "Keine Bitmaps auf SD"
#define TR_NO_SCRIPTS_ON_SD            "Keine Skripte auf SD"
#define TR_SCRIPT_SYNTAX_ERROR         TR("Syntaxfehler", "Skript Syntaxfehler")
#define TR_SCRIPT_PANIC                "Skript Panik"
#define TR_SCRIPT_KILLED               "Skript beendet"
#define TR_SCRIPT_ERROR                "Unbekannter Fehler"
#define TR_PLAY_FILE                   "Abspielen"
#define TR_DELETE_FILE                 "Löschen"
#define TR_COPY_FILE                   "Kopieren"
#define TR_RENAME_FILE                 "Umbenennen"
#define TR_ASSIGN_BITMAP               "Bitmap zuordnen"
#define TR_ASSIGN_SPLASH               "Als Startbild"
#define TR_EXECUTE_FILE                "Ausführen"
#define TR_REMOVED                     " gelöscht"
#define TR_SD_INFO                     "Information"
#define TR_NA                          "N/V"    //NV=Nicht Verfügbar  Kurz-Meldung
#define TR_FORMATTING                  "Formatierung..."
#define TR_TEMP_CALIB                  "Temp.  abgl."
#define TR_TIME                        "Uhrzeit:"
#define TR_MAXBAUDRATE                 "Max Baud"
#define TR_BAUDRATE                    "Baudrate"
#define TR_CRSF_ARMING_MODE            "Arm via"
#define TR_CRSF_ARMING_MODES           TR_CH"5", TR_SWITCH
#define TR_SAMPLE_MODE                 TR_BW_COL("Abtastmod.", "Abtastmodus")
#define TR_SAMPLE_MODES_1              "Normal"
#define TR_SAMPLE_MODES_2              "OneBit"
#define TR_LOADING                     "Wird geladen..."
#define TR_DELETE_THEME                "Theme löschen?"
#define TR_SAVE_THEME                  "Theme speichern?"
#define TR_EDIT_COLOR                  "Farbe bearbeiten"
#define TR_NO_THEME_IMAGE              "Kein Theme Bild"
#define TR_BACKLIGHT_TIMER             "Inaktivitäts Timeout"

#define TR_MODEL_QUICK_SELECT          "Schnelle Modellauswahl"
#define TR_LABELS_SELECT               "Labelauswahl"
#define TR_LABELS_MATCH                "Labelvergleich"
#define TR_FAV_MATCH                   "Favoriten vergleichen"
#define TR_LABELS_SELECT_MODE_1        "Mehrfachauswahl"
#define TR_LABELS_SELECT_MODE_2        "Einfachauswahl"
#define TR_LABELS_MATCH_MODE_1         "Alle"
#define TR_LABELS_MATCH_MODE_2         "Beliebig"
#define TR_FAV_MATCH_MODE_1            "Muss übereinstimmen"
#define TR_FAV_MATCH_MODE_2            "Alternative Übereinstimmung"

#define TR_SELECT_TEMPLATE_FOLDER      "Wähle Vorlagenverzeichnis:"
#define TR_SELECT_TEMPLATE             "Wähle Modellvorlage:"
#define TR_NO_TEMPLATES                "Es wurden keine Modellvorlagen in diesem Verzeichnis gefunden"
#define TR_SAVE_TEMPLATE               "Als Vorlage speichern"
#define TR_BLANK_MODEL                 "Leeres Modell"
#define TR_BLANK_MODEL_INFO            "Erzeuge leeres Modell"
#define TR_FILE_EXISTS                 "Datei existiert schon"
#define TR_ASK_OVERWRITE               "Möchten Sie überschreiben?"

#define TR_BLUETOOTH                   "Bluetooth"
#define TR_BLUETOOTH_DISC              "Suchen"
#define TR_BLUETOOTH_INIT              "Init"
#define TR_BLUETOOTH_DIST_ADDR         "Dist addr"
#define TR_BLUETOOTH_LOCAL_ADDR        "Local addr"
#define TR_BLUETOOTH_PIN_CODE          "PIN Code"
#define TR_BLUETOOTH_NODEVICES         "kein Gerät gefunden"
#define TR_BLUETOOTH_SCANNING          "Suche..."
#define TR_BLUETOOTH_BAUDRATE          "BT Baudrate"
#define TR_BLUETOOTH_MODES_1           "---"
#define TR_BLUETOOTH_MODES_2           "Telemetrie"
#define TR_BLUETOOTH_MODES_3           "Trainer"
#define TR_BLUETOOTH_MODES_4           "Aktiviert"
#define TR_SD_INFO_TITLE               "SD-INFO"
#define TR_SD_SPEED                    "Geschw:"
#define TR_SD_SECTORS                  "Sektoren:"
#define TR_SD_SIZE                     "Größe:"
#define TR_TYPE                        "Typ"
#define TR_GVARS                       TR_BW_COL("GLOBALE V.", "Globale V.")
#define TR_GLOBAL_VAR                  "Globale Variable"
#define TR_OWN                         "Eigen"
#define TR_DATE                        "Datum:"
#define TR_MONTHS_1                    "Jan"
#define TR_MONTHS_2                    "Feb"
#define TR_MONTHS_3                    "Mar"
#define TR_MONTHS_4                    "Apr"
#define TR_MONTHS_5                    "Mai"
#define TR_MONTHS_6                    "Jun"
#define TR_MONTHS_7                    "Jul"
#define TR_MONTHS_8                    "Aug"
#define TR_MONTHS_9                    "Sep"
#define TR_MONTHS_10                   "Okt"
#define TR_MONTHS_11                   "Nov"
#define TR_MONTHS_12                   "Dez"
#define TR_ROTARY_ENCODER              "Drehg."
#define TR_ROTARY_ENC_MODE             TR("Drehg. Modus","Drehgeber Modus")
#define TR_CHANNELS_MONITOR            "Kanal-Monitor==>"
#define TR_MIXERS_MONITOR              "==>Mischer Monitor"
#define TR_PATH_TOO_LONG               "Pfad zu lang"
#define TR_VIEW_TEXT                   "Zeige Text"
#define TR_FLASH_BOOTLOADER            "Flash Bootloader"
#define TR_FLASH_DEVICE                "Flash Gerät"
#define TR_FLASH_EXTERNAL_DEVICE       TR("Flash ext. Gerät","Flash externes Gerät")
#define TR_FLASH_RECEIVER_OTA          "Flash Empfänger OTA"
#define TR_FLASH_RECEIVER_BY_EXTERNAL_MODULE_OTA "Flash RX via ext. OTA"
#define TR_FLASH_RECEIVER_BY_INTERNAL_MODULE_OTA "Flash RX via int. OTA"
#define TR_FLASH_FLIGHT_CONTROLLER_BY_EXTERNAL_MODULE_OTA "Flash FC via ext. OTA"
#define TR_FLASH_FLIGHT_CONTROLLER_BY_INTERNAL_MODULE_OTA "Flash FC via int. OTA"
#define TR_FLASH_BLUETOOTH_MODULE      TR("Flash BT module", "Flash Bluetoothmodul")
#define TR_FLASH_POWER_MANAGEMENT_UNIT TR("Flash PMU", "Flash PMU Firmware")
#define TR_DEVICE_NO_RESPONSE          "Gerät antwortet nicht"
#define TR_DEVICE_FILE_ERROR           "G.-Dateiproblem"
#define TR_DEVICE_DATA_REFUSED         "G.-Daten abg."
#define TR_DEVICE_WRONG_REQUEST        "G.-Zugriffsfehler"
#define TR_DEVICE_FILE_REJECTED        "G.-Datei abg."
#define TR_DEVICE_FILE_WRONG_SIG       "G.-Datei Sig."
#define TR_CURRENT_VERSION             TR("Aktuelle Vers. ", "Aktuelle Version: ")
#define TR_FLASH_INTERNAL_MODULE       TR("Flash int. XJT","Flash int. XJT-Modul")
#define TR_FLASH_INTERNAL_MULTI        TR("Flash int. Multi", "Flash int. Multimodul")
#define TR_FLASH_EXTERNAL_MODULE       TR("Flash ext. mod","Flash ext. Modul")
#define TR_FLASH_EXTERNAL_MULTI        TR("Flash ext. Multi", "Flash ext. Multimodul")
#define TR_FLASH_EXTERNAL_ELRS         "Flash ext. ELRS"
#define TR_FIRMWARE_UPDATE_ERROR       "Firmware Updatefehler"
#define TR_FIRMWARE_UPDATE_SUCCESS     "Update erfolgreich"
#define TR_WRITING                     "Schreibe..."
#define TR_CONFIRM_FORMAT              "Formatieren bestätigen?"
#define TR_INTERNALRF                  "Internes HF-Modul"
#define TR_INTERNAL_MODULE             TR("Int. Modul", "Internes Modul")
#define TR_EXTERNAL_MODULE             TR("Ext. Modul", "Externes Modul")
#define TR_EDGETX_UPGRADE_REQUIRED     "EdgeTX upgrade nötig"
#define TR_TELEMETRY_DISABLED          "Deaktiv. Telem."  //more chars doesn't fit on QX7
#define TR_MORE_OPTIONS_AVAILABLE      "mehr Optionen verfügbar"
#define TR_NO_MODULE_INFORMATION       "keine Modul Info"
#define TR_EXTERNALRF                  "Externes HF-Modul"
#define TR_FAILSAFE                    TR("Failsafe", "Failsafe Mode")
#define TR_FAILSAFESET                 "Failsafe setzen"
#define TR_REG_ID                      TR("Reg. ID", "Registration ID")
#define TR_OWNER_ID                    TR("Eigent. ID", "Eigentümer-ID")
#define TR_HOLD                        "Hold"
#define TR_HOLD_UPPERCASE              "HOLD"
#define TR_NONE                        "Kein"
#define TR_NONE_UPPERCASE              "KEIN"
#define TR_MENUSENSOR                  TR_BW_COL("SENSOR", "Sensor")
#define TR_POWERMETER_PEAK             "Spitze"
#define TR_POWERMETER_POWER            "Leistung"
#define TR_POWERMETER_ATTN             "Dämpf."
#define TR_POWERMETER_FREQ             "Freq."
#define TR_MENUTOOLS                   TR_BW_COL("TOOLS", "Tools")
#define TR_MIC_RECORDER                TR_BW_COL("Mikrofon Rek.", "Mikrofon Rekorder")
#define TR_PUSH_TO_RECORD              TR_BW_COL("Drücken=Aufnahme", "Drücken zum Aufnehmen")
#define TR_RECORD                      "Aufnahme"
#define TR_STOP                        "Stop"
#define TR_REC                         "REC"
#define TR_STARTING_IN                 "Start in"
#define TR_GET_READY                   "Bereit machen..."
#define TR_SAVED                       "Gespeichert:"
#define TR_SAVE_AS                     "Speichern als"
#define TR_AUTO_TRIM                   "Auto-Schnitt"
#define TR_TRIM_START                  "Anfang kürzen"
#define TR_TRIM_END                    "Ende kürzen"
#define TR_OPEN_ERROR                  "Öffnungsfehler"
#define TR_TURN_OFF_RECEIVER           "Empf. ausschalten"
#define TR_STOPPING                    "Stoppe..."
#define TR_MENU_SPECTRUM_ANALYSER      TR_BW_COL("SPEKTRUM ANALYSATOR", "Spektrum Analysator")
#define TR_MENU_POWER_METER            TR_BW_COL("LEISTUNGSMESSER", "Leistungsmesser")
#define TR_SENSOR                      TR_BW_COL("SENSOR", "Sensor")
#define TR_COUNTRY_CODE                "Landescode"
#define TR_USBMODE                     "USB Modus"
#define TR_USB_CHARGE                 "Laden wenn Sender an"
#define TR_JACK_MODE                   "Klinken-Modus"
#define TR_VOICE_LANGUAGE              "Sprachansagen"
#define TR_TEXT_LANGUAGE               "Textsprache"
#define TR_UNITS_SYSTEM                "Einheiten"
#define TR_UNITS_PPM                   "PPM Einheiten"
#define TR_EDIT                        "Zeile Editieren"
#define TR_INSERT_BEFORE               "Neue Zeile davor"
#define TR_INSERT_AFTER                "Neue Zeile danach"
#define TR_COPY                        "Zeile kopieren"
#define TR_MOVE                        "Zeile verschieben"
#define TR_PASTE                       "Zeile einfügen"
#define TR_PASTE_AFTER                 "Einfügen danach"
#define TR_PASTE_BEFORE                "Einfügen davor"
#define TR_DELETE                      "Zeile löschen"
#define TR_INSERT                      "Neue Zeile"
#define TR_RESET_FLIGHT                "Reset Flugdaten"
#define TR_RESET_TIMER1                "Reset Timer1"
#define TR_RESET_TIMER2                "Reset Timer2"
#define TR_RESET_TIMER3                "Reset Timer3"
#define TR_RESET_TELEMETRY             "Reset Telemetrie"
#define TR_STATISTICS                  "Statistik Timer Gas"
#define TR_ABOUT_US                    "Die Programmierer"
#define TR_USB_JOYSTICK                "USB Joystick (HID)"
#define TR_USB_MASS_STORAGE            "USB Speicher (SD)"
#define TR_USB_SERIAL                  "USB Seriell (VCP)"
#define TR_SETUP_SCREENS               "Setup Hauptbildschirme"
#define TR_MONITOR_SCREENS             "Monitore Mischer Kanal Logik"
#define TR_AND_SWITCH                  TR("UND Schalt", "UND Schalter") // UND mit weiterem Schaltern
#define TR_SF                          "SF" // Spezial Funktionen
#define TR_GF                          "GF" // Globale Funktionen
#define TR_ANADIAGS_CALIB              "analoge Geber Kalibriert"
#define TR_ANADIAGS_FILTRAWDEV         "analoge Geber gefiltert und unbearbeitet mit Abweichungen"
#define TR_ANADIAGS_UNFILTRAW          "analoge Geber ungefiltert und unbearbeitet"
#define TR_ANADIAGS_MINMAX             "Min., Max. und Bereich"
#define TR_ANADIAGS_MOVE               "Analog-Eingänge voll ausschlagen"
#define TR_BYTES                       "Bytes"
#define TR_MODULE_BIND                 BUTTON(TR("Bnd","Binden"))   //9XR-Pro
#define TR_MODULE_UNBIND               BUTTON("Trennen")
#define TR_POWERMETER_ATTN_NEEDED      "Dämpfungsgl. nötig"
#define TR_PXX2_SELECT_RX              "Wähle RX"
#define TR_PXX2_DEFAULT                "<Standard>"
#define TR_BT_SELECT_DEVICE            "Wähle Gerät"
#define TR_DISCOVER                    BUTTON("Suche")
#define TR_BUTTON_INIT                 BUTTON("Init")
#define TR_WAITING                     "Warte..."
#define TR_RECEIVER_DELETE             "Empfänger löschen?"
#define TR_RECEIVER_RESET              "Empfänger resetten?"
#define TR_SHARE                       "Teilen"
#define TR_BIND                        "Binden"
#define TR_REGISTER                    BUTTON(TR("Reg", "Registrieren"))
#define TR_MODULE_RANGE                BUTTON(TR("Rng", "Reichweite"))
#define TR_RANGE_TEST                  "Reichweitentest"
#define TR_RECEIVER_OPTIONS            TR_BW_COL("RX OPTIONEN", "RX Optionen")
#define TR_RESET_BTN                   BUTTON("Reset")
#define TR_KEYS_BTN                    BUTTON(TR("SW","Schalter"))
#define TR_ANALOGS_BTN                 BUTTON("Analog")
#define TR_FS_BTN                      BUTTON(TR("AnpSchalt", TR_FUNCTION_SWITCHES))
#define TR_TOUCH_NOTFOUND              "Touch Hardware nicht gefunden"
#define TR_TOUCH_EXIT                  "Berühre Bildschirm zum Beenden"
#define TR_SET                         BUTTON("Set")
#define TR_TRAINER                     "Lehrer/Schüler"
#define TR_CHANS                       "Chans"
#define TR_ANTENNAPROBLEM              "TX-Antennenproblem!"
#define TR_MODELIDUSED                 "ID benutzt in:"
#define TR_MODELIDUNIQUE               "ID ist eindeutig"
#define TR_MODULE                      "Modul-Typ"
#define TR_RX_NAME                     "Rx Name"
#define TR_TELEMETRY_TYPE              TR("Typ", "Telemetrietyp")
#define TR_TELEMETRY_SENSORS           "Sensoren"
#define TR_VALUE                       "Wert"
#define TR_PERIOD                      "Periode"
#define TR_INTERVAL                    "Intervall"
#define TR_REPEAT                      "Wiederholung"
#define TR_ENABLE                      "Aktivieren"
#define TR_DISABLE                     "Deaktivieren"
#define TR_TOPLCDTIMER                 "oberer LCD Timer"
#define TR_UNIT                        "Einheit"
#define TR_TELEMETRY_NEWSENSOR         "Sensor hinzufügen"
#define TR_CHANNELRANGE                TR("Kanäle", "Ausgangs Kanäle")  //wg 9XR-Pro
#define TR_ANTENNACONFIRM1             "Ant. umschalten"
#define TR_ANTENNA_MODES_1             "Intern"
#define TR_ANTENNA_MODES_2             "Frag"
#define TR_ANTENNA_MODES_3             "Modellspezifisch"
#define TR_ANTENNA_MODES_4             "Intern + Extern"
#define TR_ANTENNA_MODES_5             "Extern"
#define TR_ANTENNA_SELECT             "Intern","Extern"
#define TR_USE_INTERNAL_ANTENNA        TR("Nutze int. Antenne", "Int. Antenne verwenden")
#define TR_USE_EXTERNAL_ANTENNA        TR("Nutze ext. Antenne", "Ext. Antenne verwenden")
#define TR_ANTENNACONFIRM2             TR("Check Antenne", "Ist eine externe Antenne installiert?")
#define TR_MODULE_PROTOCOL_FLEX_WARN_LINE1   "Benötigt non"
#define TR_MODULE_PROTOCOL_FCC_WARN_LINE1    "Benötigt FCC"
#define TR_MODULE_PROTOCOL_EU_WARN_LINE1     "Benötigt EU"
#define TR_MODULE_PROTOCOL_WARN_LINE2        "Zert. Firmware"
#define TR_LOWALARM                    TR("1.Warnschwelle", "Erste Warnschwelle")
#define TR_CRITICALALARM               "Kritischer Alarm"
#define TR_DISABLE_ALARM               TR("Alarme AUS", "Telemetrie Alarme AUS")
#define TR_POPUP                       "Popup"
#define TR_MIN                         "Min"
#define TR_MAX                         "Max"
#define TR_CURVE_PRESET                "Gerade 0 11 22 33 45"
#define TR_PRESET                      "Voreinst."
#define TR_MIRROR                      "Spiegeln"
#define TR_CLEAR                       "Löschen"
#define TR_CLEAR_BTN                   BUTTON("Löschen")
#define TR_RESET                       TR("Servowert reset","Servowerte zurücksetzen")
#define TR_RESET_SUBMENU               TR("Reset Werte   ==>", "Reset=>Timer Flug Telem")
#define TR_COUNT                       "Punkte"
#define TR_PT                          "Pt"
#define TR_PTS                         "Pts"
#define TR_SMOOTH                      "Runden"
#define TR_COPY_STICKS_TO_OFS          TR("Kop. Knüppel->Subtrim", "Kopiere Knüppel zu Subtrim")
#define TR_COPY_MIN_MAX_TO_OUTPUTS     TR3("Kop. min/max auf alle", "Kopiere min/max zu allen" , "Kopiere Limits & Mitte auf alle Kanäle")
#define TR_COPY_TRIMS_TO_OFS           TR3("Kop. Trim->Subtrim",  "Kopiere Trimm zu Subtrim" , "Kopiere Trimmposition auf Subtrim")  // "Trim to Subtrim"
#define TR_INCDEC                      "Inc/Decrement"
#define TR_GLOBALVAR                   "Global Var"
#define TR_MIXSOURCE                   "Quelle (%)"
#define TR_MIXSOURCERAW                "Quelle (Wert)"
#define TR_CONSTANT                    "Konstant"
#define TR_PREFLIGHT_POTSLIDER_CHECK_1 "Aus"
#define TR_PREFLIGHT_POTSLIDER_CHECK_2 "Ein"
#define TR_PREFLIGHT_POTSLIDER_CHECK_3 "Auto"
#define TR_PREFLIGHT                   "Vorflug-Checkliste"
#define TR_CHECKLIST                   TR("Checkliste", "Checkliste anzeigen")
#define TR_CHECKLIST_INTERACTIVE       TR3("C-Interaktiv", "Interakt. Checkl.", "Interaktive Checkliste")
#define TR_AUX_SERIAL_MODE             "Serieller Port"
#define TR_AUX2_SERIAL_MODE            "Serieller Port 2"
#define TR_AUX_SERIAL_PORT_POWER       "Versorgung"
#define TR_SCRIPT                      "Lua-Skript"
#define TR_INPUTS                      "Eingaben"
#define TR_OUTPUTS                     "Ausgaben"
#define TR_CONFIRMRESET                TR("Alles löschen? ","ALLE Modelle+Einst. löschen?")
#define TR_TOO_MANY_LUA_SCRIPTS        "Zu viele Skripte!"
#define TR_SPORT_UPDATE_POWER_MODE     "SP Power"
#define TR_SPORT_UPDATE_POWER_MODES_1  "AUTO"
#define TR_SPORT_UPDATE_POWER_MODES_2  "EIN"
#define TR_NO_TELEMETRY_SCREENS        "Keine Telemetrie Seiten"
#define TR_TOUCH_PANEL                 "Touch panel:"
#define TR_FILE_SIZE                   "Dateigröße"
#define TR_FILE_OPEN                   "trotzdem öffnen?"

// Horus and Taranis specific column headers
#define TR_PHASES_HEADERS_NAME         "Name "
#define TR_PHASES_HEADERS_SW           "Schalter"
#define TR_PHASES_HEADERS_RUD_TRIM     "Trim Seite"
#define TR_PHASES_HEADERS_ELE_TRIM     "Trim Höhe"
#define TR_PHASES_HEADERS_THT_TRIM     "Trim Gas"
#define TR_PHASES_HEADERS_AIL_TRIM     "Trim Quer"
#define TR_PHASES_HEADERS_CH5_TRIM     "Trim 5"
#define TR_PHASES_HEADERS_CH6_TRIM     "Trim 6"
#define TR_PHASES_HEADERS_FAD_IN       "Langs Ein"
#define TR_PHASES_HEADERS_FAD_OUT      "Langs Aus"

#define TR_LIMITS_HEADERS_NAME         "Name"
#define TR_LIMITS_HEADERS_SUBTRIM      "Subtrim"
#define TR_LIMITS_HEADERS_MIN          "Min"
#define TR_LIMITS_HEADERS_MAX          "Max"
#define TR_LIMITS_HEADERS_DIRECTION    "Richtung"
#define TR_LIMITS_HEADERS_CURVE        "Kurve"
#define TR_LIMITS_HEADERS_PPMCENTER    "PPM Mitte"
#define TR_LIMITS_HEADERS_SUBTRIMMODE  "Subtrim Modus"
#define TR_INVERTED                    "Invertiert"

// Horus layouts and widgets
#define TR_FIRST_CHANNEL               "Erster Kanal"
#define TR_LAST_CHANNEL                "Letzter Kanal"
#define TR_FILL_BACKGROUND             "Hintergrund füllen?"
#define TR_BG_COLOR                    "Hintergrundfarbe"
#define TR_SLIDERS_TRIMS               "Schieber+Trim"
#define TR_SLIDERS                     "Schieber"
#define TR_FLIGHT_MODE                 "Flugphase"
#define TR_INVALID_FILE                "ungültige Datei"
#define TR_TIMER_SOURCE                "Timer Quelle"
#define TR_SIZE                        "Größe"
#define TR_SHADOW                      "Schatten"
#define TR_ALIGNMENT                   "Ausrichtung"
#define TR_ALIGN_LABEL                 "Name ausrichten"
#define TR_ALIGN_VALUE                 "Wert ausrichten"
#define TR_ALIGN_OPTS_1                "Links"
#define TR_ALIGN_OPTS_2                "Mitte"
#define TR_ALIGN_OPTS_3                "Rechts"
#define TR_TEXT                        "Text"
#define TR_COLOR                       "Farbe"
#define TR_PANEL1_BACKGROUND           "Panel1 Hintergrund"
#define TR_PANEL2_BACKGROUND           "Panel2 Hintergrund"
#define TR_PANEL_BACKGROUND            "Hintergrund"
#define TR_PANEL_COLOR                 " Farbe"
#define TR_WIDGET_GAUGE                "Pegel"
#define TR_WIDGET_MODELBMP             "Modellinfo"
#define TR_WIDGET_OUTPUTS              "Ausgänge"
#define TR_WIDGET_TEXT                 "Text"
#define TR_WIDGET_TIMER                "Timer"
#define TR_WIDGET_VALUE                "Wert"

// About screen
#define TR_ABOUTUS                     TR(" Info ", "Info")

#define TR_CHR_HOUR                    'h' // Stunden
#define TR_CHR_INPUT                   'I' // Values between A-I will work

#define TR_BEEP_VOLUME                 "Piep-Lautst."
#define TR_WAV_VOLUME                  "Wav-Lautst."
#define TR_BG_VOLUME                   TR("Bgr-Lautst.", "Hintergrund-Lautstärke")

#define TR_TOP_BAR                     "Infozeile"
#define TR_FLASH_ERASE                 "Flash löschen..."
#define TR_FLASH_WRITE                 "Flash schreiben..."
#define TR_OTA_UPDATE                  "OTA Update..."
#define TR_MODULE_RESET                "Modul reset..."
#define TR_UNKNOWN_RX                  "unbekannter RX"
#define TR_UNSUPPORTED_RX              "nicht unterstützter RX"
#define TR_OTA_UPDATE_ERROR            "OTA Update Fehler"
#define TR_DEVICE_RESET                "Gerät Reset..."
#define TR_ALTITUDE                    "Höhenanzeige"
#define TR_SCALE                       "Skalieren"
#define TR_VIEW_CHANNELS               "Zeige Kanäle"
#define TR_VIEW_NOTES                  "Zeige Notizen"
#define TR_MODEL_SELECT                "Modell auswählen"
#define TR_ID                          "ID"
#define TR_PRECISION                   "Präzision"
#define TR_RATIO                       "Umrechnung"  //Faktor, Mulitplikator, Teiler  0,1 bis 10,0
#define TR_FORMULA                     "Formel"
#define TR_CELLINDEX                   "Zellenindex"
#define TR_LOGS                        "Log Daten"
#define TR_OPTIONS                     "Optionen"
#define TR_FIRMWARE_OPTIONS            BUTTON("Firmwareoptionen")

#define TR_ALTSENSOR                   "Höhen Sensor"
#define TR_CELLSENSOR                  "Zellen Sensor"
#define TR_GPSSENSOR                   "GPS Sensor"
#define TR_GYRO                        "Gyro"
#define TR_CURRENTSENSOR               "Sensor"
#define TR_AUTOOFFSET                  "Auto Offset"
#define TR_ONLYPOSITIVE                "Nur Positiv"
#define TR_FILTER                      "Filter aktiv"
#define TR_TELEMETRYFULL               TR("Telem voll!", "Telemetriezeilen voll!")
#define TR_IGNORE_INSTANCE             TR("Ign. Inst.", "Ignor. Instanzen")
#define TR_SHOW_INSTANCE_ID            "Zeige Instanz ID"
#define TR_DISCOVER_SENSORS            "Start Sensorsuche"
#define TR_STOP_DISCOVER_SENSORS       "Stop Sensorsuche"
#define TR_DELETE_ALL_SENSORS          "Lösche alle Sensoren"
#define TR_CONFIRMDELETE               "Wirklich alle " LCDW_128_LINEBREAK "löschen ?"
#define TR_SELECT_WIDGET               "Widget auswählen"  // grafisches Element
#define TR_WIDGET_FULLSCREEN           "Vollbild"
#define TR_REMOVE_WIDGET               "Widget löschen"
#define TR_WIDGET_SETTINGS             "Widget einstellen"
#define TR_REMOVE_SCREEN               "Seite löschen"
#define TR_SETUP_WIDGETS               "Widget einrichten"
#define TR_THEME                       "Theme"
#define TR_SETUP                       "Einrichten"
#define TR_LAYOUT                      "Layout"
#define TR_TEXT_COLOR                  "Textfarbe"
// ----------------------------- Symbole für Auswahlliste----------
#define TR_MENU_INPUTS                 CHAR_INPUT "Geber"
#define TR_MENU_LUA                    CHAR_LUA "Lua-Skripte"
#define TR_MENU_STICKS                 CHAR_STICK "Knüppel"
#define TR_MENU_POTS                   CHAR_POT "Potis"
#define TR_MENU_MIN                    CHAR_FUNCTION "MIN"
#define TR_MENU_MAX                    CHAR_FUNCTION "MAX"
#define TR_MENU_HELI                   CHAR_CYC "Heli-TS CYC1-3"
#define TR_MENU_TRIMS                  CHAR_TRIM "Trimmung"
#define TR_MENU_SWITCHES               CHAR_SWITCH "Schalter"
#define TR_MENU_LOGICAL_SWITCHES       CHAR_SWITCH "Log. Schalter"
#define TR_MENU_TRAINER                CHAR_TRAINER "Trainer"
#define TR_MENU_CHANNELS               CHAR_CHANNEL "Kanäle"
#define TR_MENU_GVARS                  CHAR_SLIDER "Glob. Vars"
#define TR_MENU_TELEMETRY              CHAR_TELEMETRY "Telemetrie"
#define TR_MENU_DISPLAY                "TELM-SEITEN"
#define TR_MENU_OTHER                  "Weitere"
#define TR_MENU_INVERT                 TR_BW_COL("Invert.", "Invertieren")
#define TR_AUDIO_MUTE                  TR("Ton Stumm","Geräuschunterdrückung")
#define TR_JITTER_FILTER               "ADC Filter"
#define TR_DEAD_ZONE                   "Dead zone"
#define TR_RTC_CHECK                   TR("RTC Prüfen", "RTC Spannung prüfen")
#define TR_AUTH_FAILURE                "Auth-Fehler"
#define TR_RACING_MODE                 "Racing mode"

#define TR_USE_THEME_COLOR             "Farbe des Themes verwenden"

#define TR_ADD_ALL_TRIMS_TO_SUBTRIMS   "Alle Trimmungen übernehmen"
#define TR_DUPLICATE                   "Duplizieren"
#define TR_ACTIVATE                    "Aktivieren"
#define TR_RED                         "Rot"
#define TR_BLUE                        "Blau"
#define TR_GREEN                       "Grün"
#define TR_COLOR_PICKER                "Farbauswahl"
#define TR_FIXED                       "Fixed"
#define TR_EDIT_THEME_DETAILS          "Theme Details Bearb."
#define TR_THEME_COLOR_DEFAULT         "Standard"
#define TR_THEME_COLOR_PRIMARY1        "Primär1"
#define TR_THEME_COLOR_PRIMARY2        "Primär2"
#define TR_THEME_COLOR_PRIMARY3        "Primär3"
#define TR_THEME_COLOR_SECONDARY1      "Sekundär1"
#define TR_THEME_COLOR_SECONDARY2      "Sekundär2"
#define TR_THEME_COLOR_SECONDARY3      "Sekundär3"
#define TR_THEME_COLOR_FOCUS           "Fokus"
#define TR_THEME_COLOR_EDIT            "Edit"
#define TR_THEME_COLOR_ACTIVE          "Aktiv"
#define TR_THEME_COLOR_WARNING         "Warnung"
#define TR_THEME_COLOR_DISABLED        "Deaktiviert"
#define TR_THEME_COLOR_QM_BG           "Quick Menü HG"
#define TR_THEME_COLOR_QM_FG           "Quick Menü VG"
#define TR_THEME_COLOR_CUSTOM          "Eigene"
#define TR_THEME_CHECKBOX              "Schalter"
#define TR_THEME_ACTIVE                "Aktiv"
#define TR_THEME_REGULAR               "Regulär"
#define TR_THEME_WARNING               "Warnung"
#define TR_THEME_DISABLED              "Inaktiv"
#define TR_THEME_EDIT                  "Editieren"
#define TR_THEME_FOCUS                 "Fokus"
#define TR_AUTHOR                      "Author"
#define TR_DESCRIPTION                 "Beschreibung"
#define TR_SAVE                        "Speichern"
#define TR_CANCEL                      "Abbruch"
#define TR_EDIT_THEME                  "THEME Editieren"
#define TR_DETAILS                     "Details"

// Voice in native language
#define TR_VOICE_ENGLISH               "Englisch"
#define TR_VOICE_CHINESE               "Chinesisch"
#define TR_VOICE_CZECH                 "Tschechisch"
#define TR_VOICE_DANISH                "Dänisch"
#define TR_VOICE_DEUTSCH               "Deutsch"
#define TR_VOICE_DUTCH                 "Holländisch"
#define TR_VOICE_ESPANOL               "Spanisch"
#define TR_VOICE_FINNISH               "Finnish"
#define TR_VOICE_FRANCAIS              "Französisch"
#define TR_VOICE_HUNGARIAN             "Ungarisch"
#define TR_VOICE_ITALIANO              "Italienisch"
#define TR_VOICE_POLISH                "Polnisch"
#define TR_VOICE_PORTUGUES             "Portugiesisch"
#define TR_VOICE_RUSSIAN               "Russisch"
#define TR_VOICE_SLOVAK                "Slowenisch"
#define TR_VOICE_SWEDISH               "Schwedisch"
#define TR_VOICE_TAIWANESE             "Taiwanese"
#define TR_VOICE_JAPANESE              "Japanisch"
#define TR_VOICE_HEBREW                "Hebräisch"
#define TR_VOICE_UKRAINIAN             "Ukrainisch"
#define TR_VOICE_KOREAN                "Koreanisch"

#define TR_USBJOYSTICK_LABEL           "USB Joystick"
#define TR_USBJOYSTICK_EXTMODE         "Modus"
#define TR_VUSBJOYSTICK_EXTMODE_1      "Klassisch"
#define TR_VUSBJOYSTICK_EXTMODE_2      "Erweitert"
#define TR_USBJOYSTICK_SETTINGS        BUTTON("Kanal Einstellungen")
#define TR_USBJOYSTICK_IF_MODE         TR("If.Modus","Interface Modus")
#define TR_VUSBJOYSTICK_IF_MODE_1      "Joystick"
#define TR_VUSBJOYSTICK_IF_MODE_2      "Gamepad"
#define TR_VUSBJOYSTICK_IF_MODE_3      "MultiAchsen"
#define TR_USBJOYSTICK_CH_MODE         "Modus"
#define TR_VUSBJOYSTICK_CH_MODE_1      "Kein"
#define TR_VUSBJOYSTICK_CH_MODE_2      "Tasten"
#define TR_VUSBJOYSTICK_CH_MODE_3      "Achse"
#define TR_VUSBJOYSTICK_CH_MODE_4      "Sim"
#define TR_VUSBJOYSTICK_CH_MODE_S_1    "-"
#define TR_VUSBJOYSTICK_CH_MODE_S_2    "B"
#define TR_VUSBJOYSTICK_CH_MODE_S_3    "A"
#define TR_VUSBJOYSTICK_CH_MODE_S_4    "S"
#define TR_USBJOYSTICK_CH_BTNMODE      "Tasten Modus"
#define TR_VUSBJOYSTICK_CH_BTNMODE_1   "Normal"
#define TR_VUSBJOYSTICK_CH_BTNMODE_2   "Puls"
#define TR_VUSBJOYSTICK_CH_BTNMODE_3   "SWEmu"
#define TR_VUSBJOYSTICK_CH_BTNMODE_4   "Delta"
#define TR_VUSBJOYSTICK_CH_BTNMODE_5   "Companion"
#define TR_VUSBJOYSTICK_CH_BTNMODE_S_1 TR("Norm","Normal")
#define TR_VUSBJOYSTICK_CH_BTNMODE_S_2 TR("Puls","Puls")
#define TR_VUSBJOYSTICK_CH_BTNMODE_S_3 TR("SWEm","SWEmul")
#define TR_VUSBJOYSTICK_CH_BTNMODE_S_4 TR("Delt","Delta")
#define TR_VUSBJOYSTICK_CH_BTNMODE_S_5 TR("CPN","Companion")
#define TR_USBJOYSTICK_CH_SWPOS        "Positionen"
#define TR_VUSBJOYSTICK_CH_SWPOS_1     "Drücken"
#define TR_VUSBJOYSTICK_CH_SWPOS_2     "2POS"
#define TR_VUSBJOYSTICK_CH_SWPOS_3     "3POS"
#define TR_VUSBJOYSTICK_CH_SWPOS_4     "4POS"
#define TR_VUSBJOYSTICK_CH_SWPOS_5     "5POS"
#define TR_VUSBJOYSTICK_CH_SWPOS_6     "6POS"
#define TR_VUSBJOYSTICK_CH_SWPOS_7     "7POS"
#define TR_VUSBJOYSTICK_CH_SWPOS_8     "8POS"
#define TR_USBJOYSTICK_CH_AXIS         "Achse"
#define TR_VUSBJOYSTICK_CH_AXIS_1      "X"
#define TR_VUSBJOYSTICK_CH_AXIS_2      "Y"
#define TR_VUSBJOYSTICK_CH_AXIS_3      "Z"
#define TR_VUSBJOYSTICK_CH_AXIS_4      "rotX"
#define TR_VUSBJOYSTICK_CH_AXIS_5      "rotY"
#define TR_VUSBJOYSTICK_CH_AXIS_6      "rotZ"
#define TR_VUSBJOYSTICK_CH_AXIS_7      "Schieber"
#define TR_VUSBJOYSTICK_CH_AXIS_8      "Dial"
#define TR_VUSBJOYSTICK_CH_AXIS_9      "Rad"
#define TR_USBJOYSTICK_CH_SIM          "Sim Achse"
#define TR_VUSBJOYSTICK_CH_SIM_1       "Quer"
#define TR_VUSBJOYSTICK_CH_SIM_2       "Höhe"
#define TR_VUSBJOYSTICK_CH_SIM_3       "Seite"
#define TR_VUSBJOYSTICK_CH_SIM_4       "Gas"
#define TR_VUSBJOYSTICK_CH_SIM_5       "Beschl."
#define TR_VUSBJOYSTICK_CH_SIM_6       "Bremsen"
#define TR_VUSBJOYSTICK_CH_SIM_7       "Lenkung"
#define TR_VUSBJOYSTICK_CH_SIM_8       "Dpad"
#define TR_USBJOYSTICK_CH_INVERSION    "Invers"
#define TR_USBJOYSTICK_CH_BTNNUM       "Tastennr."
#define TR_USBJOYSTICK_BTN_COLLISION   "!Tastennr. Kollision!"
#define TR_USBJOYSTICK_AXIS_COLLISION  "!Achsen Kollision!"
#define TR_USBJOYSTICK_CIRC_COUTOUT    TR("Ringbegr.", "Ring Begrbenzung")
#define TR_VUSBJOYSTICK_CIRC_COUTOUT_1 "kein"
#define TR_VUSBJOYSTICK_CIRC_COUTOUT_2 "X-Y, Z-rX"
#define TR_VUSBJOYSTICK_CIRC_COUTOUT_3 "X-Y, rX-rY"
#define TR_VUSBJOYSTICK_CIRC_COUTOUT_4 "X-Y, Z-rZ"
#define TR_USBJOYSTICK_APPLY_CHANGES   BUTTON("Änd. übernehmen")

#define TR_DIGITAL_SERVO               "Servo333HZ"
#define TR_ANALOG_SERVO                "Servo 50HZ"
#define TR_SIGNAL_OUTPUT               "Signal Ausgang"
#define TR_SERIAL_BUS                  "Serialbus"
#define TR_SYNC                        "Sync"

#define TR_ENABLED_FEATURES            "Menüpunkte"
#define TR_RADIO_MENU_TABS             "Sender-Menüpunkte"
#define TR_MODEL_MENU_TABS             "Modell-Menüpunkte"

#define TR_SELECT_MENU_ALL             "Alle"
#define TR_SELECT_MENU_CLR             "Löschen"
#define TR_SELECT_MENU_INV             "Invertiert"

#define TR_SORT_ORDERS_1               "Name A-Z"
#define TR_SORT_ORDERS_2               "Name Z-A"
#define TR_SORT_ORDERS_3               "Wenig benutzt"
#define TR_SORT_ORDERS_4               "Meist benutzt"
#define TR_SORT_MODELS_BY              "Modelle sortieren nach"
#define TR_CREATE_NEW                  "Erstelle"

#define TR_MIX_SLOW_PREC               TR("Langs. Vorlauf", "Langs. Vor-/Rücklauf")
#define TR_MIX_DELAY_PREC              TR("Verz. Vorlauf", "Verz. Vor-/Rücklauf")

#define TR_THEME_EXISTS                "Ein Theme-Verzeichnis mit demselben Namen existiert bereits"

#define TR_DATE_TIME_WIDGET            "Datum & Uhrzeit"
#define TR_RADIO_INFO_WIDGET           "Fernst. Info"
#define TR_LOW_BATT_COLOR              "Farbe Akku fast leer"
#define TR_MID_BATT_COLOR              "Farbe Akku mittel"
#define TR_HIGH_BATT_COLOR             "Farbe Akku voll"
#define TR_WIDGET_SIZE                 "Widget Größe"

#define TR_DEL_DIR_NOT_EMPTY           "Löschen nur bei leerem Verzeichnis möglich"

#define TR_KEY_SHORTCUTS               "Tastenkürzel"
#define TR_CURRENT_SCREEN              "Aktueller Bildschirm"
#define TR_SHORT_PRESS                 "Kurzer Druck"
#define TR_LONG_PRESS                  "Langer Druck"
#define TR_OPEN_QUICK_MENU             "Quick Menü öffnen"
#define TR_QUICK_MENU_FAVORITES        "Quick Menü Favoriten"

// ApexTX
#define TR_NB4_RACING             "Racing"
#define TR_NB4_BRAKE_MAX          "Max brake"
#define TR_NB4_DRAG_BRAKE         "Drag brake"
#define TR_NB4_ABS                "ABS"
#define TR_NB4_ABS_POINT          "ABS point"
#define TR_NB4_ABS_RATE           "ABS rate"
#define TR_NB4_ABS_RELEASE        "ABS release"
#define TR_NB4_STEER_TURN         "Turn speed"
#define TR_NB4_STEER_RETURN       "Return speed"
#define TR_NB4_IDLE_UP            "Idle up"
#define TR_NB4_IDLE_UP_SW         "Idle up switch"
#define TR_NB4_ENGINE_CUT         "Engine cut"
#define TR_NB4_CUT_POS            "Cut position"
#define TR_NB4_LAP_SW             "Lap switch"
#define TR_NB4_LAPS               "Laps"
#define TR_NB4_CONDITION          "Condition"
#define TR_NB4_RACE               "Race"
#define TR_NB4_MODE            "Mode"
#define TR_NB4_MODES           "Compact","Full"
#define TR_NB4_PRESETS            "Starting point"
#define TR_NB4_ELECTRIC           "Electric"
#define TR_NB4_NITRO              "Nitro"
#define TR_NB4_PRESET_ASK         "Overwrite the racing settings of this model?"
#define TR_NB4_SET_HOME           "Racing home"
#define TR_NB4_SET_HOME_ASK       "Replace screen 1 with the racing home?"
#define TR_NB4_START_POINT        "Starting point"
#define TR_NB4_SUMMARY            "Race summary"
#define TR_NB4_QUICK              "Quick adjust"
#define TR_NB4_RESET_RACE         "New race"
#define TR_NB4_RESET_RACE_ASK     "Clear the laps of this race?"
#define TR_NB4_BEST               "Best"
#define TR_NB4_AVERAGE            "Average"
#define TR_NB4_TOTAL              "Total"
#define TR_NB4_NO_LAPS            "No laps yet"
#define TR_NB4_ADDRESS_BYTE       "AFHDS3 frame"
#define TR_NB4_ADDRESS_MODES      "With address,No address"
#define TR_NB4_ANNOUNCE           "Announce lap"
#define TR_NB4_GOT_IT                "Got it"
#define TR_NB4_WHAT_EACH_SETTING_DOES "?  What each setting does"
#define TR_NB4_PALETTE               "PALETTE"
#define TR_NB4_ACCENT                "ACCENT"
#define TR_NB4_EXTERNAL_THEMES       "External themes"
#define TR_NB4_ORIENTATION           "ORIENTATION"
#define TR_NB4_PORTRAIT              "Portrait"
#define TR_NB4_LANDSCAPE             "Landscape"
#define TR_NB4_BUILT_IN_MINIMAL_INTERFACE_CAR_CONFIGURA "Built-in minimal interface. Car configuration is preserved."
#define TR_NB4_SYSTEM_DIAGNOSTICS    "System & diagnostics"
#define TR_NB4_CAR                   "Car"
#define TR_NB4_RF_DISABLED           "RF DISABLED"
#define TR_NB4_UNSUPPORTED_MODEL_THE_ORIGINAL_FILE_IS "Unsupported model. The original file is preserved. Select a car model."
#define TR_NB4_MODELS                "Models"
#define TR_NB4_RESULT_PENDING        "Result pending"
#define TR_NB4_SAVING_RUN            "Saving run..."
#define TR_NB4_RUN_SAVED             "Run saved"
#define TR_NB4_VIEW_RACE_HISTORY     "View race history"
#define TR_NB4_STORAGE_UNAVAILABLE_RETRY "Storage unavailable • retry"
#define TR_NB4_SAVE_FAILED_RETRY     "Save failed • retry"
#define TR_NB4_SAVE_PENDING_RUNS_FIRST "Save pending runs first"
#define TR_NB4_LAP                   "LAP"
#define TR_NB4_TIME                  "TIME"
#define TR_NB4_TIMERS_LAPS           "Timers & laps"
#define TR_NB4_RACE_5527             "Race"
#define TR_NB4_SAVE_PENDING_RESULTS_THEN_RETRY "Save pending results, then retry."
#define TR_NB4_MARK_LAP              "Mark lap"
#define TR_NB4_UNDO_LAP              "Undo lap"
#define TR_NB4_SETUP                 "Setup"
#define TR_NB4_FINISH                "Finish"
#define TR_NB4_NEW_RUN               "New run"
#define TR_NB4_START                 "Start"
#define TR_NB4_FUEL_PACK             "FUEL / PACK"
#define TR_NB4_DURATION_MIN          "Duration (min)"
#define TR_NB4_REFUELLED_NEW_PACK    "Refuelled / new pack"
#define TR_NB4_TIMER_2_COUNTDOWN_RX_SUPPLY_IS "Timer 2 countdown. RX supply is the BEC; it does not measure pack charge."
#define TR_NB4_SET_A_DURATION        "Set a duration"
#define TR_NB4_LAPS_LEFT             "Laps left: --"
#define TR_NB4_ESTIMATED_LAPS        "Estimated laps:"
#define TR_NB4_RACE_HISTORY          "Race history"
#define TR_NB4_LOADING               "Loading..."
#define TR_NB4_HISTORY_COULD_NOT_BE_READ_CHECK "History could not be read. Check storage or the USB connection."
#define TR_NB4_LAPS_8959             "laps"
#define TR_NB4_DATE_UNAVAILABLE      "Date unavailable"
#define TR_NB4_NO_SAVED_RUNS_YET     "No saved runs yet"
#define TR_NB4_INVALID_RECORD        "Invalid record"
#define TR_NB4_OLDER_RUNS            "Older runs"
#define TR_NB4_LATEST_RUNS           "Latest runs"
#define TR_NB4_REFRESH_RETRY         "Refresh / retry"
#define TR_NB4_BACKUP_RESTORE        "Backup & restore"
#define TR_NB4_COPY_RADIO_MODELS_SCRIPTS_THEMES_AND "Copy RADIO, MODELS, SCRIPTS, THEMES and LOGS using Files or USB before testing firmware.\n\nThe settings before Racing are retained in RADIO/radio-pre-racing-ui-v2.yml.\n\nTo roll back, power off, restore your backup files and use the package's rollback firmware. Keep data and firmware versions together."
#define TR_NB4_OPEN_FILES            "Open files"
#define TR_NB4_FACTORY_RESET         "FACTORY RESET"
#define TR_NB4_AXIS_CALIBRATION_AND_THE_INTERFACE_LANGU "Axis calibration and the interface language are always kept: no recalibrating, and the menus stay in your language."
#define TR_NB4_RADIO_SETTINGS        "Radio settings"
#define TR_NB4_RETURNS_THEME_HOME_SCREEN_SOUND_UNITS "Returns theme, home screen, sound, units and advanced options to factory.\n\nDoes not touch cars or calibration."
#define TR_NB4_THIS_CAR              "This car"
#define TR_NB4_RETURNS_THE_OPEN_CAR_TO_FACTORY "Returns the open car to factory values: travel, curves, brake, ABS and engine.\n\nKeeps the name. Does not touch other cars or the radio."
#define TR_NB4_RADIO_AND_THIS_CAR    "Radio and this car"
#define TR_NB4_BOTH_AT_ONCE_YOUR_OTHER_SAVED "Both at once.\n\nYour other saved cars stay where they are."
#define TR_NB4_TELEMETRY             "Telemetry"
#define TR_NB4_SENSORS               "Sensors"
#define TR_NB4_LOG_FILES             "Log files"
#define TR_NB4_SELECT_A_SENSOR       "Select a sensor"
#define TR_NB4_STALE                 " • Stale"
#define TR_NB4_NO_DATA               " • No data"
#define TR_NB4_READING_HAS_NO_NUMERIC_CHART "Reading has no numeric chart"
#define TR_NB4_NO_SENSORS_CONFIGURED "No sensors configured"
#define TR_NB4_MORE_SENSORS          "More sensors"
#define TR_NB4_FIRST_SENSORS         "First sensors"
#define TR_NB4_PAUSED                "Paused"
#define TR_NB4_RUNNING               "Running"
#define TR_NB4_FINISHED              "Finished"
#define TR_NB4_READY                 "Ready"
#define TR_NB4_FINISHED_7B5C         "Finished"
#define TR_NB4_DISABLED              "Disabled"
#define TR_NB4_READY_84E2            "Ready"
#define TR_NB4_STEERING              "STEERING"
#define TR_NB4_STEERING_ANGLE        "STEERING ANGLE"
#define TR_NB4_THR                   "THR"
#define TR_NB4_BRAKE                 "/ BRAKE"
#define TR_NB4_BRAKE_B80D            "BRAKE"
#define TR_NB4_FORWARD               "FORWARD"
#define TR_NB4_REVERSE               "REVERSE"
#define TR_NB4_TIMER                 "TIMER"
#define TR_NB4_CURRENT_LAP           "CURRENT LAP"
#define TR_NB4_COUNTDOWN             "COUNTDOWN"
#define TR_NB4_ELAPSED               "ELAPSED"
#define TR_NB4_BEST_7CAE             "BEST"
#define TR_NB4_LAST                  "LAST"
#define TR_NB4_LAPS_7AA1             "LAPS"
#define TR_NB4_SESSION               "SESSION"
#define TR_NB4_RACE_9606             "RACE"
#define TR_NB4_RACE_TIME             "RACE TIME"
#define TR_NB4_TIMER_A026            "TIMER"
#define TR_NB4_VS_BEST               "VS BEST"
#define TR_NB4_TIME_REMAINING        "Time remaining"
#define TR_NB4_ELAPSED_TIME          "Elapsed time"
#define TR_NB4_BOTH_SIDES_SHARE_ONE_SETTING "Both sides share one setting"
#define TR_NB4_A_VALUE_COMES_FROM_ANOTHER_SOURCE "A value comes from another source, not a number"
#define TR_NB4_THIS_SETUP_CANNOT_BE_EDITED_HERE "This setup cannot be edited here"
#define TR_NB4_OPEN_INPUTS           "Open Inputs"
#define TR_NB4_OPEN_MIXES            "Open Mixes"
#define TR_NB4_OPEN_OUTPUTS          "Open Outputs"
#define TR_NB4_TURN_THESE_FUNCTIONS_OFF "Turn these functions off"
#define TR_NB4_BOTH_SIDES_USE_CURVE_S_AND "Both sides use curve %s, and %d more setting%s in the model does too. Moving its points moves them all."
#define TR_NB4_BOTH_SIDES_USE_CURVE_S_MOVING "Both sides use curve %s. Moving its points changes throttle and brake together; changing one side's selector does not."
#define TR_NB4_EDIT_THE_POINTS_OF_S  "Edit the points of %s"
#define TR_NB4_THIS_AFFECTS_THROTTLE_BRAKE_AND_D "This affects throttle, brake and %d more setting%s. Continue?"
#define TR_NB4_THIS_AFFECTS_THROTTLE_AND_BRAKE_AT "This affects throttle and brake at once. Continue?"
#define TR_NB4_THIS_CHANNEL_S_MIX_CARRIES_AN "This channel's mix carries an offset or a curve that the drawing does not include, so it is not drawn: better none than a map that lies."
#define TR_NB4_BRAKE_30D6            "Brake"
#define TR_NB4_LEFT                  "Left"
#define TR_NB4_NEUTRAL               "Neutral"
#define TR_NB4_CENTRE                "Centre"
#define TR_NB4_THROTTLE              "Throttle"
#define TR_NB4_RIGHT                 "Right"
#define TR_NB4_THROTTLE_AND_BRAKE_CURVE "Throttle and brake curve"
#define TR_NB4_STEERING_CURVE        "Steering curve"
#define TR_NB4_THIS_SETTING_CANNOT_BE_EDITED_WHILE "This setting cannot be edited while this channel's mixer chain cannot be interpreted."
#define TR_NB4_TRAVEL                "Travel"
#define TR_NB4_CURVE                 "Curve"
#define TR_NB4_SPEED                 "Speed"
#define TR_NB4_REVERSE_CHANNEL       "Reverse channel"
#define TR_NB4_TRIM                  "Trim"
#define TR_NB4_DUAL_RATE             "Dual rate"
#define TR_NB4_EXPONENTIAL           "Exponential"
#define TR_NB4_LEFT_DUAL_RATE        "Left dual rate"
#define TR_NB4_RIGHT_DUAL_RATE       "Right dual rate"
#define TR_NB4_LEFT_EXPO             "Left expo"
#define TR_NB4_RIGHT_EXPO            "Right expo"
#define TR_NB4_THROTTLE_TRACKING     "Throttle tracking"
#define TR_NB4_WHICH_CHANNEL_THE_TIMER_FOLLOWS_TO "Which channel the timer follows to count engine time."
#define TR_NB4_ENGINE                "Engine"
#define TR_NB4_SIDE                  "+ side"
#define TR_NB4_SIDE_1C91             "- side"
#define TR_NB4_RESPONSE_BOTH         "Response (both)"
#define TR_NB4_THROTTLE_EXPO         "Throttle expo"
#define TR_NB4_EXPO_SIDE             "Expo, + side"
#define TR_NB4_BRAKE_EXPO            "Brake expo"
#define TR_NB4_EXPO_SIDE_F0FC        "Expo, - side"
#define TR_NB4_THIS_MODEL_IS_DECLARED_ELECTRIC_NO "This model is declared electric: no idle-up and no engine cut. The type is set in Racing."
#define TR_NB4_CHANNELS              "Channels"
#define TR_NB4_WHICH_RECEIVER_OUTPUT_EACH_CONTROL_DRIVE "Which receiver output each control drives, and whether that channel comes out reversed."
#define TR_NB4_US                    "(us)"
#define TR_NB4_PERCENT_UNIT                  "(%)"
#define TR_NB4_REVERSE_TRIGGER       "Reverse trigger"
#define TR_NB4_WHEEL_TRIM            "Wheel trim"
#define TR_NB4_TRIGGER_TRIM          "Trigger trim"
#define TR_NB4_VEHICLE_TYPE          "Vehicle type"
#define TR_NB4_RESPONSE              "Response"
#define TR_NB4_CUSTOM                "Custom"
#define TR_NB4_NOT_SET               "Not set"
#define TR_NB4_THIS_SETTING_TAKES_ITS_VALUE_FROM "This setting takes its value from a model variable. The GV button turns it back into a figure."
#define TR_NB4_THIS_SETTING_TAKES_ITS_VALUE_FROM_CE79 "This setting takes its value from a model variable. To turn it back into a figure, switch variables on under Advanced > Model variables."
#define TR_NB4_THIS_SETTING_TAKES_ITS_VALUE_FROM_98CC "This setting takes its value from another control. The SRC button turns it back into a figure."
#define TR_NB4_BIND_RECEIVER         "Bind receiver"
#define TR_NB4_1_3_PREPARING_RADIO   "1/3 Preparing radio"
#define TR_NB4_2_3_SEARCHING_FOR_RECEIVER "2/3 Searching for receiver"
#define TR_NB4_3_3_CONFIRMING_CONNECTION "3/3 Confirming connection"
#define TR_NB4_3_3_FINISH_BINDING    "3/3 Finish binding"
#define TR_NB4_RECEIVER_CONNECTED    "Receiver connected"
#define TR_NB4_BINDING_NOT_COMPLETED "Binding not completed"
#define TR_NB4_PREPARING_COMMUNICATION_WITH_THE_RECEIVE "Preparing communication with the receiver."
#define TR_NB4_POWER_THE_RECEIVER_WHILE_HOLDING_ITS "Power the receiver while holding its bind button."
#define TR_NB4_RECEIVER_SAVED_WAITING_FOR_CONNECTION_CO "Receiver saved. Waiting for connection confirmation."
#define TR_NB4_ONE_WAY_PRESS_FINISH_WHEN_THE "One way: press Finish when the LED flashes slowly."
#define TR_NB4_CONNECTION_CONFIRMED_AND_SAVED_IN_THIS "Connection confirmed and saved in this model."
#define TR_NB4_CLOSE_AND_BIND_AGAIN  ". Close and bind again."
#define TR_NB4_STATUS                "STATUS"
#define TR_NB4_ONE_WAY_FINISH_WHEN_THE_LED "One way: finish when the LED flashes slowly."
#define TR_NB4_CANCEL                "Cancel"
#define TR_NB4_BIND                  "Bind"
#define TR_NB4_RANGE                 "Range"
#define TR_NB4_PLAYBACK              "Playback"
#define TR_NB4_TEST                  "Test"
#define TR_NB4_TONES_ONLY            "Tones only"
#define TR_NB4_VOICE_TONES           "Voice + tones"
#define TR_NB4_TEST_AUDIO            "Test audio"
#define TR_NB4_RIGHT_GRIP            "Right grip"
#define TR_NB4_LEFT_GRIP             "Left grip"
#define TR_NB4_LEFT_OF_STEERING_WHEEL "Left of steering wheel"
#define TR_NB4_RIGHT_OF_STEERING_WHEEL "Right of steering wheel"
#define TR_NB4_FORWARD_BACK_TRIM     "Forward / back trim"
#define TR_NB4_LEFT_RIGHT_TRIM       "Left / right trim"
#define TR_NB4_BACK                  "Back"
#define TR_NB4_SELECT_OPEN_SETTINGS  "Select / open settings"
#define TR_NB4_MODEL_SWITCH          "Model switch"
#define TR_NB4_ORIGINAL_TRIM         "Original trim"
#define TR_NB4_HOLD                  " (hold)"
#define TR_NB4_PRESS_A_CONTROL       "Press a control"
#define TR_NB4_PRESS_A_BUTTON_OR_A_FOUR "Press a button or a four-way trim. It will be assigned automatically."
#define TR_NB4_RELEASE_ANY_CONTROLS_YOU_ARE_ALREADY "Release any controls you are already holding."
#define TR_NB4_MULTIPLE_CONTROLS_PRESSED_RELEASE_THEM_A "Multiple controls pressed. Release them and press just one."
#define TR_NB4_WAITING_FOR_A_CONTROL_U_S "Waiting for a control... %u s"
#define TR_NB4_ASSIGN_FUNCTION       "Assign function"
#define TR_NB4_1_CHOOSE_A_FUNCTION_2_PRESS "1. Choose a function. 2. Press a control."
#define TR_NB4_FUNCTION              "Function"
#define TR_NB4_HOLD_0_6_S            "Hold 0.6 s"
#define TR_NB4_ON_PRESS              "On press"
#define TR_NB4_THE_SW_SIGNAL_REMAINS_AVAILABLE_TO "The SW signal remains available to model channels, mixes and functions."
#define TR_NB4_ENGINE_CUT_F024       "Engine cut. "
#define TR_NB4_IDLE_UP_23E4          "Idle up. "
#define TR_NB4_LAPS_A8F4             "Laps. "
#define TR_NB4_TIMERS                "Timers. "
#define TR_NB4_MIXES                 "Mixes. "
#define TR_NB4_SPECIAL_FUNCTIONS     "Special functions. "
#define TR_NB4_ALSO_ASSIGNED         "Also assigned: "
#define TR_NB4_SAVE_ASSIGNMENT       "Save assignment"
#define TR_NB4_ASSIGN_BY_PRESSING    "Assign by pressing"
#define TR_NB4_NO_CONTROL_DETECTED_TRY_AGAIN "No control detected. Try again."
#define TR_NB4_ASSIGNED              "Assigned: "
#define TR_NB4_ITS_CHANNEL_ASSIGNMENTS_ARE_KEPT ". Its channel assignments are kept."
#define TR_NB4_VIEW_BY_CONTROL       "View by control"
#define TR_NB4_PREVIOUS_NEXT_MOVES_FOCUS_SELECT_OPENS "Previous / next moves focus. Select opens the focused control; Back closes the view."
#define TR_NB4_HOLD_TO_CLEAR_TIMER_1_AND "Hold to clear Timer 1 and the current race laps."
#define TR_NB4_CONTROLS_TIMER_1_AND_LAPS_PAUSE "Controls Timer 1 and laps. Pause keeps elapsed time; press again to continue the race."
#define TR_NB4_USES_THE_NATIVE_TRIM_STEP_AND "Uses the native trim step and repeat. Replaces this key's original function."
#define TR_NB4_ORIGINAL_FUNCTION_RESTORES_THIS_KEY_S "Original function restores this key's usual behaviour."
#define TR_NB4_KEYS_AND_NAVIGATION   "Keys and navigation"
#define TR_NB4_ASSIGNMENTS           "Assignments"
#define TR_NB4_PER_CAR_TAP_A_CONTROL_TO "Per car. Tap a control to change its function."
#define TR_NB4_USE_WHEEL_GRIP_OR_TRIM_BUTTONS "Use wheel, grip or trim buttons to navigate. Touch remains available."
#define TR_NB4_BUTTONS               "BUTTONS"
#define TR_NB4_SW2_SW3_PAIR          "SW2 / SW3 pair"
#define TR_NB4_SW2_DOWN_SW3_UP       "SW2 down / SW3 up"
#define TR_NB4_NAVIGATION_PREVIOUS_NEXT "Navigation: previous / next"
#define TR_NB4_STEERING_TRIM         "Steering trim - / +"
#define TR_NB4_THROTTLE_TRIM         "Throttle trim - / +"
#define TR_NB4_RESTORE_BOTH          "Restore both"
#define TR_NB4_OTHER_CONTROLS_AND_CHANNELS "Other controls and channels"
#define TR_NB4_OTHER_CONTROLS        "Other controls"
#define TR_NB4_WHEEL_AND_TRIGGER     "Wheel and trigger"
#define TR_NB4_MIXES_60C8            "Mixes"
#define TR_NB4_SWITCHES              "Switches"
#define TR_NB4_ROTARY_CONTROLS_KEEP_THEIR_CURRENT_ROLE "Rotary controls keep their current role. TR4 is not yet read by this firmware."
#define TR_NB4_RIGHT_GRIP_8E97       "  Right grip"
#define TR_NB4_LEFT_GRIP_23DC        "  Left grip"
#define TR_NB4_WHEEL_LEFT            "  Wheel left"
#define TR_NB4_WHEEL_RIGHT           "  Wheel right"
#define TR_NB4_POWER                 "Power"
#define TR_NB4_GENERAL_PREFERENCES   "General preferences"
#define TR_NB4_USB                   "USB"
#define TR_NB4_CONTROL_BEHAVIOUR     "Control behaviour"
#define TR_NB4_BRIGHTNESS            "Brightness"
#define TR_NB4_SOUND                 "Sound"
#define TR_NB4_ALERTS                "Alerts"
#define TR_NB4_HAPTIC                "Haptic"
#define TR_NB4_LOCATION              "Location"
#define TR_NB4_DATE_LOCATION         "Date & location"
#define TR_NB4_UPDATE                "Update"
#define TR_NB4_THE_RADIO_RESTARTS_INTO_UPDATE_MODE "The radio restarts in update mode. Connect USB and open ApexTX Updater on your computer."
#define TR_NB4_MODEL_VARIABLES       "Model variables"
#define TR_NB4_USE_VARIABLES         "Use variables"
#define TR_NB4_OPEN_THE_VARIABLE_EDITOR "Open the variable editor"
#define TR_NB4_A_VARIABLE_IS_A_NAMED_NUMBER "A variable is a named number stored in the model. Several settings can take their value from it instead of carrying their own figure: change the variable and they all change."
#define TR_NB4_A_TWO_CHANNEL_CAR_RARELY_NEEDS "A two-channel car rarely needs them, and switched on they put a \"GV\" button next to every number: pressing it stops that setting being a figure. That is why they ship switched off."
#define TR_NB4_LIGHTS                "Lights"
#define TR_NB4_MODE_5032             "Mode"
#define TR_NB4_FIXED_COLOUR          "Fixed colour"
#define TR_NB4_BREATHING             "Breathing"
#define TR_NB4_BATTERY_STATE         "Battery state"
#define TR_NB4_OFF                   "Off"
#define TR_NB4_COLOUR                "Colour"
#define TR_NB4_RED                   "Red"
#define TR_NB4_ORANGE                "Orange"
#define TR_NB4_YELLOW                "Yellow"
#define TR_NB4_GREEN                 "Green"
#define TR_NB4_CYAN                  "Cyan"
#define TR_NB4_BLUE                  "Blue"
#define TR_NB4_MAGENTA               "Magenta"
#define TR_NB4_WHITE                 "White"
#define TR_NB4_WHILE_THE_RADIO_IS_CHARGING_THE "While the radio is charging the LED shows the charge in green -breathing while it fills, steady when full- whatever the mode. Only \"Off\" overrides that: if you switch the light off, it stays off."
#define TR_NB4_TIMERS_85E8           "Timers"
#define TR_NB4_TIMER_BF94            "Timer"
#define TR_NB4_STORAGE               "Storage"
#define TR_NB4_OPEN_BROWSER          "Open browser"
#define TR_NB4_CREATE_FILESYSTEM     "Create filesystem"
#define TR_NB4_NO_FILESYSTEM_FOUND_CREATING_ONE_ERASES "No filesystem found. Creating one ERASES everything on it: cars, settings and logs. If you think there was data, back it up over USB first."
#define TR_NB4_NOT_AVAILABLE_WITH_THE_CURRENT_RADIO "Not available with the current radio or car settings."
#define TR_NB4_QUICK_ACCESS          "Quick access"
#define TR_NB4_SETTINGS              "Settings"
#define TR_NB4_STEERING_2090         "Steering"
#define TR_NB4_THROTTLE_BRAKE        "Throttle & brake"
#define TR_NB4_CALIBRATE             "Calibrate"
#define TR_NB4_PIT                   "Pit"
#define TR_NB4_TX_RADIO              "TX  RADIO"
#define TR_NB4_RX_RECEIVER           "RX  RECEIVER"
#define TR_NB4_BATTERIES             "BATTERIES"
#define TR_NB4_DIFF                  "DIFF"
#define TR_NB4_LINK                  "LINK"
#define TR_NB4_NO_LINK               "NO LINK"
#define TR_NB4_CRITICAL              "CRITICAL"
#define TR_NB4_WEAK                  "WEAK"
#define TR_NB4_MIN                   "MIN"
#define TR_NB4_MAX                   "MAX"
#define TR_NB4_SENSOR                "SENSOR"
#define TR_NB4_PAUSED_52B4           "PAUSED"
#define TR_NB4_RUNNING_A5C9          "RUNNING"
#define TR_NB4_READY_66E1            "READY"
#define TR_NB4_AVERAGE_C834          "AVERAGE"
#define TR_NB4_THR_BRAKE             "THR / BRAKE"
#define TR_NB4_TRIMS                 "TRIMS"
#define TR_NB4_ST                    "ST"
#define TR_NB4_TH                    "TH"
#define TR_NB4_EN                    "en"
#define TR_NB4_THE_DECLARED_CHANNEL_IS_OUT_OF "the declared channel is out of range"
#define TR_NB4_SEVERAL_MIXES_FEED_THIS_CHANNEL "several mixes feed this channel"
#define TR_NB4_THE_CHANNEL_HAS_NO_MIX_AT "the channel has no mix at all"
#define TR_NB4_THE_MIX_DEPENDS_ON_A_SWITCH "the mix depends on a switch"
#define TR_NB4_THE_MIX_DOES_NOT_GO_THROUGH "the mix does not go through an input, so expo is not applied"
#define TR_NB4_THE_MIX_WEIGHT_IS_A_REFERENCE "the mix weight is a reference"
#define TR_NB4_THE_MIX_WEIGHT_IS_ZERO "the mix weight is zero"
#define TR_NB4_AN_INPUT_LINE_DEPENDS_ON_A "an input line depends on a switch"
#define TR_NB4_THE_INPUT_DOES_NOT_COME_FROM "the input does not come from a stick"
#define TR_NB4_THE_TWO_LINES_ARE_NOT_THE "the two lines are not the same stick with the same sign"
#define TR_NB4_ONE_OF_THE_LINES_HAS_ZERO "one of the lines has zero weight"
#define TR_NB4_TWO_LINES_FIGHT_OVER_THE_POSITIVE "two lines fight over the positive side"
#define TR_NB4_TWO_LINES_FIGHT_OVER_THE_NEGATIVE "two lines fight over the negative side"
#define TR_NB4_THE_INPUT_HAS_NO_LINE_AT "the input has no line at all"
#define TR_NB4_THE_INPUT_HAS_MORE_THAN_TWO "the input has more than two lines"
#define TR_NB4_ONE_SIDE_OF_THE_STICK_HAS "one side of the stick has no line, and that side is dead"
#define TR_NB4_SOME_WEIGHT_OR_CURVE_VALUE_IS "some weight or curve value is a source reference"
#define TR_NB4_A_SINGLE_LINE_SERVES_BOTH_SIDES "a single line serves both sides"
#define TR_NB4_WHICH_HALF_OF_THE_TRIGGER_BRAKES "Which half of the trigger brakes is unknown, so whether brake limit and ABS would act on the brake or on the throttle is unknown too."
#define TR_NB4_ON_THIS_CHANNEL_THE_BRAKE_ARRIVES "On this channel the brake arrives with the throttle's sign. Brake limit and ABS would act on the throttle, and idle-up on the brake."
#define TR_NB4_THE_TRIGGER_S_SIGN_CANNOT_BE "The trigger's sign cannot be followed to the output, so there is no way to know which half brake and ABS would act on."
#define TR_NB4_GENERAL "General"
#define TR_NB4_SAFETY "Safety"
#define TR_NB4_NOTES "Notes"
#define TR_NB4_THIS_CAR_HAS_NO_NOTES_YET_THEY_ARE_READ "This car has no notes yet. They are read from a text file named after it inside the card's MODELS folder: create it over USB and it will show up here."
#define TR_NB4_BRAKE_ABS "Brake & ABS"
#define TR_NB4_RF_MODULE_RECEIVER_FAILSAFE "RF module, receiver & failsafe"
#define TR_NB4_TRIMS_LABEL "Trims"
#define TR_NB4_CONFIGURE_QUICK_ACCESS "Configure quick access"
#define TR_NB4_CHOOSING_WHAT_GOES_INTO_QUICK_ACCESS_IS "Choosing what goes into Quick access is not built yet: the eight entries are fixed for now. Quick access itself works: it is the spanner on the home screen."
#define TR_NB4_MONITOR "Monitor"
#define TR_NB4_TELEMETRY_IS_SWITCHED_OFF_FOR_THIS_CAR_T "Telemetry is switched off for this car. Turn it on in the radio settings' view options."
#define TR_NB4_TRACK_VIEW "Track view"
#define TR_NB4_STATISTICS "Statistics"
#define TR_NB4_HISTORY "History"
#define TR_NB4_RUN_SUMMARY "Run summary"
#define TR_NB4_THE_RUN_SUMMARY_IS_NOT_BUILT_YET_IN_THE "The run summary is not built yet. In the meantime every saved run opens in full under Race > History."
#define TR_NB4_SESSION_RESETS "Session resets"
#define TR_NB4_MANAGE "Manage"
#define TR_NB4_TEMPLATES "Templates"
#define TR_NB4_TEMPLATES_HAVE_NO_PAGE_OF_THEIR_OWN_YET "Templates have no page of their own yet. You pick one when creating a new car, from Models > Manage."
#define TR_NB4_TOP_BAR "Top bar"
#define TR_NB4_SCREENS "Screens"
#define TR_NB4_THEME "Theme"
#define TR_NB4_EXTERNAL_THEMES_ARE_SWITCHED_OFF_TURN_TH "External themes are switched off. Turn them on in the radio settings' view options. The NB4 palette and accent live in Screen > Home, which does not depend on this."
#define TR_NB4_HOME "Home"
#define TR_NB4_BLUETOOTH "Bluetooth"
#define TR_NB4_THIS_RADIO_HAS_NO_BLUETOOTH_IT_IS_NOT_A "This radio has no Bluetooth. It is not a missing setting: the NB4 firmware is built without it because the board has no module."
#define TR_NB4_HARDWARE "Hardware"
#define TR_NB4_CALIBRATION "Calibration"
#define TR_NB4_DATE_TIME_LOCATION "Date, time & location"
#define TR_NB4_DIAGNOSTICS "Diagnostics"
#define TR_NB4_ABOUT "About"
#define TR_NB4_HELP "Help"
#define TR_NB4_THERE_IS_NO_HELP_INDEX_YET_THE_ONE_SHEET "There is no help index yet. The one sheet written so far is inside Receiver > RF, under the \"What each setting does\" button."
#define TR_NB4_INPUTS "Inputs"
#define TR_NB4_OUTPUTS "Outputs"
#define TR_NB4_CURVES "Curves"
#define TR_NB4_POINT_CURVES_ARE_SWITCHED_OFF_TURN_THEM "Point curves are switched off. Turn them on in the radio settings' view options. The throttle and steering curves do not depend on this: they live on their own pages."
#define TR_NB4_LOGIC "Logic"
#define TR_NB4_LOGICAL_SWITCHES_ARE_SWITCHED_OFF_TURN_T "Logical switches are switched off. Turn them on in the radio settings' view options."
#define TR_NB4_MODEL_SPECIAL_FUNCTIONS "Model special functions"
#define TR_NB4_SPECIAL_FUNCTIONS_ARE_SWITCHED_OFF_TURN "Special functions are switched off. Turn them on in the radio settings' view options."
#define TR_NB4_MODEL_VARIABLES_GVAR "Model variables (GVAR)"
#define TR_NB4_SCRIPTS "Scripts"
#define TR_NB4_SCRIPTS_ARE_SWITCHED_OFF_TURN_THEM_ON_IN "Scripts are switched off. Turn them on in the radio settings' view options."
#define TR_NB4_RECEIVER "Receiver"
#define TR_NB4_CONTROLS "Controls"
#define TR_NB4_DISPLAY "Display"
#define TR_NB4_CONNECTION "Connection"
#define TR_NB4_SYSTEM "System"
#define TR_NB4_ADVANCED "Advanced"
#define TR_NB4_THE_PROTOCOL_THE_RADIO_USES_TO_TALK_TO_T "The protocol the radio uses to talk to the receiver. The NB4 internal module is FlySky AFHDS3, which is what the bundled receivers speak. Set to Off, the radio stops transmitting and the car will not respond."
#define TR_NB4_MODULE_STATUS "Module status"
#define TR_NB4_WHAT_THE_LINK_IS_DOING_RIGHT_NOW_CONNECT "What the link is doing right now. \"Connected\": the receiver answers. \"Disconnected\": no receiver powered, or not bound. \"Binding\": the radio is waiting for the receiver to pair."
#define TR_NB4_TYPE "Type"
#define TR_NB4_CHOOSE_THE_FAMILY_SUPPORTED_BY_YOUR_RECE "Choose the family supported by your receiver and bind again after changing it. Classic: FGr4, FGr4S, FGr4P, FTr4, FTr10 and FTr16S. Enhanced: FGr4B, FGr8B, FGr12B, FTr8B, FTr12B, GMr and TMr. Set the output count in Channels. The module's regional configuration is preserved."
#define TR_NB4_MODULE_OPTIONS "Module options"
#define TR_NB4_HOW_THE_RECEIVER_DRIVES_ITS_PINS_PWM_ONE "How the receiver drives its pins: PWM (one servo per pin), PPM, serial bus (SBUS, i-BUS), and the servo frame rate. 50 Hz is for analogue servos and 333 Hz for digital ones; feeding 333 Hz to an analogue servo can burn it. If you are not sure which you have, leave it at 50 Hz."
#define TR_NB4_THE_TELEMETRY_THE_RECEIVER_SENDS_BACK_PA "The telemetry the receiver sends back: pack voltage, temperature, RPM. Whatever shows up here is what you can put on the home screen and use for alarms."
#define TR_NB4_WHICH_OF_THE_RADIO_S_CHANNELS_ARE_SENT_T "Which of the radio's channels are sent to the receiver. On a car you normally leave this alone: steering and throttle are the first two, and the rest only matter if the car has extras (gears, lights, lockers)."
#define TR_NB4_FAILSAFE "Failsafe"
#define TR_NB4_WHAT_THE_CAR_DOES_WHEN_THE_SIGNAL_IS_LOS "What the car does when the signal is lost. \"Hold\" leaves the servos where they were: if it was accelerating, it keeps accelerating. \"Custom\" lets you set every channel, and is the only safe choice on a car: throttle at zero, or even a little brake. \"No pulses\" stops driving the servos. \"Receiver\" uses whatever the receiver has stored."
#define TR_NB4_RECEIVER_DATA_IS_DETECTED_DURING_BINDING "Receiver data is detected during binding and saved with this model. You do not need to enter an ID."
#define TR_NB4_PAIRS_RADIO_AND_RECEIVER_THE_DIALOG_SHOW "Pairs radio and receiver. The dialog shows preparation, search and confirmation. Two-way binding closes after connection is confirmed; for one way, press Finish when the LED flashes slowly. Bind again after changing Classic/Enhanced or one/two way."
#define TR_NB4_THE_NUMBER_IDENTIFIES_THIS_MODEL_INSIDE "The number identifies THIS model inside the radio. If two models share a number the radio warns you, because the receiver could answer to the wrong one."
#define TR_NB4_PAIRS_RADIO_AND_RECEIVER_A_DIALOG_OPENS "Pairs radio and receiver. A dialog opens showing the link state: power the receiver while holding its bind button and wait for \"Connected\". You have to repeat it if you change Type or region."
#define TR_NB4_DELIBERATELY_DROPS_THE_POWER_SO_YOU_CAN "Deliberately drops the power so you can see at what distance the link breaks. Do it before running, car on the ground and motor disconnected."
#define TR_NB4_RF_AND_RECEIVER "RF and receiver"
#define TR_NB4_CLUSTER "Cluster"
#define TR_NB4_THROTTLE_GAUGE_WITH_TRIM_STEERING_SCALE "Throttle gauge with trim, steering scale and timer"
#define TR_NB4_ESSENTIAL "Essential"
#define TR_NB4_TWO_SCALES_AND_THE_TIMER_NOTHING_ELSE "Two scales and the timer, nothing else"
#define TR_NB4_PREVIOUS_HOME "Previous home"
#define TR_NB4_THE_CLASSIC_SCREEN "The classic screen"
#define TR_NB4_CHRONO "Chrono"
#define TR_NB4_CURRENT_LAP_LAST_BEST_AND_DELTA "Current lap, last, best and delta"
#define TR_NB4_FUEL_OR_PACK_TIMER_AND_LAPS_LEFT "Fuel or pack, timer and laps left"
#define TR_NB4_PACK_SIGNAL_RECEIVER_AND_TEMPERATURE "Pack, signal, receiver and temperature"
#define TR_NB4_BENCH "Bench"
#define TR_NB4_ALL_EIGHT_CHANNELS_WITH_THEIR_REAL_OUTPU "All eight channels with their real output, for setting up"
#define TR_NB4_DEFAULT "Default"
#define TR_NB4_RACE_TIMER "Race timer"
#define TR_NB4_METRIC_CURRENT_LAP "Current lap"
#define TR_NB4_LAST_LAP "Last lap"
#define TR_NB4_BEST_LAP "Best lap"
#define TR_NB4_DELTA "Delta"
#define TR_NB4_LAP_COUNT "Lap count"
#define TR_NB4_PIT_COUNTDOWN "Pit countdown"
#define TR_NB4_METRIC_LAPS_LEFT "Laps left"
#define TR_NB4_TX_BATTERY "TX battery"
#define TR_NB4_RX_BATTERY "RX battery"
#define TR_NB4_PACK_VOLTAGE "Pack voltage"
#define TR_NB4_SIGNAL "Signal"
#define TR_NB4_TEMPERATURE "Temperature"
#define TR_NB4_RPM "RPM"
#define TR_NB4_CLOCK "Clock"
#define TR_NB4_METRIC_STEERING_TRIM "Steering trim"
#define TR_NB4_METRIC_THROTTLE_TRIM "Throttle trim"
#define TR_NB4_ORIGINAL_FUNCTION "Original function"
#define TR_NB4_NO_ACTION "No action"
#define TR_NB4_PREVIOUS "Previous"
#define TR_NB4_NEXT "Next"
#define TR_NB4_SELECT "Select"
#define TR_NB4_OPEN_SETTINGS "Open settings"
#define TR_NB4_START_PAUSE "Start / pause"
#define TR_NB4_FINISH_RACE "Finish race"
#define TR_NB4_RESET_TIMER "Reset timer"
#define TR_NB4_UNDO_LAP_ACTION "Undo lap"
#define TR_NB4_STEERING_MINUS "Steering -"
#define TR_NB4_STEERING_PLUS "Steering +"
#define TR_NB4_THROTTLE_MINUS "Throttle -"
#define TR_NB4_THROTTLE_PLUS "Throttle +"
#define TR_NB4_ORIGINAL_DISABLED "Original / disabled"
#define TR_NB4_NAVIGATION "Navigation"
#define TR_NB4_TIMER_AND_LAPS "Timer and laps"
#define TR_NB4_ADJUST_TRIMS "Adjust trims"
#define TR_NB4_NIGHT_AND_GARAGE "Night and garage"
#define TR_NB4_DAYLIGHT "Daylight"
#define TR_NB4_DIRECT_SUN_MAXIMUM_CONTRAST "Direct sun: maximum contrast"
#define TR_NB4_NIGHT_RED_PRESERVES_NIGHT_VISION "Night red: preserves night vision"
#define TR_NB4_TRACK_GREEN "Track green"
#define TR_NB4_NOBLE_ORANGE "Noble orange"
#define TR_NB4_BLACK_AND_WHITE "Black and white"
#define TR_NB4_PALETTE_ACCENT "Palette"
#define TR_NB4_RECOVERY "RECOVERY"
#define TR_NB4_LUA_DISABLED "Lua disabled"
#define TR_NB4_GO_TO "Go to %s"
#define TR_NB4_SKIP_FOR_NOW "Skip for now"
#define TR_NB4_FLASH_ON_ALARM "Flash on alarm"
#define TR_NB4_ADVICE_FAILSAFE "Without failsafe the receiver holds its last command when the link drops, so a car at speed keeps going. Set it with the throttle at neutral or braking."
#define TR_NB4_ADVICE_THROTTLE "The trigger is not at neutral, so the car can pull away the moment the receiver connects. Release it before switching the car on."
#define TR_NB4_ADVICE_CONTROLS "A control is not where this car expects it, which can leave engine cut or idle up active without you noticing. Move it back, or change the warning in Safety."
#define TR_NB4_ADVICE_STORAGE_FULL "The card is full, so lap history, logs and settings can no longer be saved. Delete old logs, or use a larger card."
#define TR_NB4_ADVICE_RADIO_DATA "The radio could not read its settings from the card and is running on defaults. Check the card, then restore a backup if you have one."
#define TR_NB4_ADVICE_SOUND_OFF "Sound is off, so the radio cannot warn you about a low battery or a lost link while you drive. Turn it back on unless you silenced it on purpose."
#define TR_NB4_ADVICE_KEY_STUCK "A control is reading as held down, which blocks the buttons. Free the control shown above; the touchscreen keeps working in the meantime."
#define TR_NB4_ADVICE_THEME "An external theme could not be loaded, so the built-in palette is in use. Choose a theme again, or leave the built-in one."
#define TR_NB4_CONTROL_LATENCY "Control latency"
