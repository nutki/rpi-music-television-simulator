add_executable (mpvplayer wait.c loadpng.c dbus.c dispmanx.c terminput.c)
find_library(LIB_BCM_HOST bcm_host PATHS /opt/vc/lib)
target_link_libraries(mpvplayer ${LIB_BCM_HOST} png dbus-1)
target_include_directories(mpvplayer PUBLIC
    /usr/include/dbus-1.0
    /usr/lib/arm-linux-gnueabihf/dbus-1.0/include
    /opt/vc/include)
