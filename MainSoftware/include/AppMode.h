#ifndef APP_MODE_H
#define APP_MODE_H

enum AppMode
{
    MAIN_SOFTWARE = 1,
    FIRMWARE_UPDATER = 2
};

AppMode appModeSelector();
const char* appModeToString(AppMode mode);

#endif // APP_MODE_H
