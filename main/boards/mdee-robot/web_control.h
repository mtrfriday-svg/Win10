#ifndef WEB_CONTROL_H
#define WEB_CONTROL_H

// Phone-friendly control page served by the robot itself (http://<robot-ip>/).
// Starts automatically once the robot has a Wi-Fi IP. Same Wi-Fi as your phone required.
class WebControl {
public:
    static void StartWhenOnline();
};

#endif
