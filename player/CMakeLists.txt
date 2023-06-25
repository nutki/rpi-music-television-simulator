add_executable(sdtv_ctl sdtv_ctl.c)
add_executable (cecinput cec.cc)
target_link_libraries(cecinput dl cec)
add_executable (mpvplayer wait.c loadpng.c dbus.c dispmanx.c terminput.c vcrfont.c comm.c teletext.c)
find_library(LIB_BCM_HOST bcm_host PATHS /opt/vc/lib)
target_link_libraries(mpvplayer ${LIB_BCM_HOST} png dbus-1 m)
target_include_directories(mpvplayer PUBLIC
    /usr/include/dbus-1.0
    /usr/lib/arm-linux-gnueabihf/dbus-1.0/include
    /opt/vc/include)
