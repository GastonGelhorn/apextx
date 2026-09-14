#
# LVGL fonts
#

if (BITMAPS_DIR STREQUAL 320x240)
set(FONT_DIR sml)
elseif (BITMAPS_DIR STREQUAL 800x480)
set(FONT_DIR lrg)
else()
set(FONT_DIR std)
endif()

file(GLOB LVGL_FONTS
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/..
  lvgl/${FONT_DIR}/lv_font_*.c)

if(PCB STREQUAL "PL18" AND PCBREV MATCHES "^NB4P?$")
  list(APPEND LVGL_FONTS
    fonts/lvgl/nb4/lv_font_nb4_gauge_22.c
    fonts/lvgl/nb4/lv_font_nb4_percent_15.c)
endif()

set(LVGL_FONT_SOURCES ${LVGL_FONTS} PARENT_SCOPE)

set(LVGL_FONT_SOURCES_MINIMAL
  fonts/lvgl/${FONT_DIR}/lv_font_bl.c
  PARENT_SCOPE
)
