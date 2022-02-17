#include <libcec/cec.h>
#include <iostream>
#include <libcec/cecloader.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
using namespace std;
using namespace CEC;

void send_keypress(char key) {
    char msg[2] = { 'K', key };
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/.mpv.socket", sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1) {
        perror("connect");
    }
    write(fd, msg, 2);
    close(fd);
}
void on_keypress(void* not_used, const cec_keypress* msg) {
    char key = 0;
    if (msg->duration) return;
    switch(msg->keycode) {
        case CEC_USER_CONTROL_CODE_SELECT: { key = 'i'; break; }
        case CEC_USER_CONTROL_CODE_UP: { key = 'w'; break; }
        case CEC_USER_CONTROL_CODE_DOWN: { key = 's'; break; }
        case CEC_USER_CONTROL_CODE_LEFT: { key = 'a'; break; }
        case CEC_USER_CONTROL_CODE_RIGHT: { key = 'd'; break; }
        case CEC_USER_CONTROL_CODE_CHANNEL_DOWN: { key = 's'; break; }
        case CEC_USER_CONTROL_CODE_CHANNEL_UP: { key = 'w'; break; }
        case CEC_USER_CONTROL_CODE_NUMBER0: { key = '0'; break; }
        case CEC_USER_CONTROL_CODE_NUMBER1: { key = '1'; break; }
        case CEC_USER_CONTROL_CODE_NUMBER2: { key = '2'; break; }
        case CEC_USER_CONTROL_CODE_NUMBER3: { key = '3'; break; }
        case CEC_USER_CONTROL_CODE_NUMBER4: { key = '4'; break; }
        case CEC_USER_CONTROL_CODE_NUMBER5: { key = '5'; break; }
        case CEC_USER_CONTROL_CODE_NUMBER6: { key = '6'; break; }
        case CEC_USER_CONTROL_CODE_NUMBER7: { key = '7'; break; }
        case CEC_USER_CONTROL_CODE_NUMBER8: { key = '8'; break; }
        case CEC_USER_CONTROL_CODE_NUMBER9: { key = '9'; break; }
        case CEC_USER_CONTROL_CODE_PLAY: { key = ' '; break; }
        case CEC_USER_CONTROL_CODE_PAUSE: { key = ' '; break; }
        case CEC_USER_CONTROL_CODE_REWIND: { key = ','; break; }
        case CEC_USER_CONTROL_CODE_FAST_FORWARD: { key = '.'; break; }
    }
//    cout << "on_keypress: " << static_cast<int>(msg->keycode) << " " << key << endl;
    if (key) send_keypress(key);
}


int main(int argc, char* argv[]) {
    ICECCallbacks        cec_callbacks;
    libcec_configuration cec_config;
    cec_config.Clear();
    cec_callbacks.Clear();

    const string devicename("MPV");
    devicename.copy(cec_config.strDeviceName, devicename.size());    
    cec_config.clientVersion       = LIBCEC_VERSION_CURRENT;
    cec_config.bActivateSource     = 0;
    cec_config.callbacks           = &cec_callbacks;
    cec_config.deviceTypes.Add(CEC_DEVICE_TYPE_RECORDING_DEVICE);
    cec_callbacks.keyPress    = &on_keypress;

    ICECAdapter* cec_adapter = LibCecInitialise(&cec_config);
    if(!cec_adapter) { 
        cerr << "Failed loading libcec.so\n"; 
        return 1; 
    }

    if(!cec_adapter->Open("RPI")) {        
        cerr << "Failed to open the CEC device on port RPI\n";
        return 1;
    }

    pause();
    return 0;
}
