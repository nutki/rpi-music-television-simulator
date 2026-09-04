add_executable(sdtv_ctl sdtv_ctl.c)
add_executable (cecinput cec.cc)
target_link_libraries(cecinput dl cec)
add_executable (mpvplayer wait.c loadpng.c dbus.c preview_shm.c dispmanx.c terminput.c vcrfont.c comm.c teletext.c)
target_compile_definitions(mpvplayer PRIVATE USE_LIBVLC)
find_library(LIB_BCM_HOST bcm_host PATHS /opt/vc/lib)
if (LIB_BCM_HOST)
    target_link_libraries(mpvplayer ${LIB_BCM_HOST})
endif()
target_link_libraries(mpvplayer png m vlc drm yuv)
target_include_directories(mpvplayer PUBLIC
    /usr/include/dbus-1.0
    /usr/lib/arm-linux-gnueabihf/dbus-1.0/include
    /opt/vc/include /usr/include/libdrm)
