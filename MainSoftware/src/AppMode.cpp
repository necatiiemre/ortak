#include "AppMode.h"
#include <iostream>

AppMode appModeSelector()
{
    int choice = 0;
    while (true)
    {
        std::cout << "Select Mode?\n";
        std::cout << "1) Main Software\n";
        std::cout << "2) Firmware Updater\n";
        std::cout << "Enter choice: ";
        std::cin >> choice;

        if (std::cin.fail())
        {
            std::cin.clear();
            std::cin.ignore(1000, '\n');
            std::cout << "Invalid input!\n\n";
            continue;
        }

        if (choice < 1 || choice > 2)
        {
            std::cout << "Invalid option! Please select between 1 - 2.\n\n";
            continue;
        }

        break;
    }

    std::cin.ignore(1000, '\n');

    AppMode mode = (AppMode)choice;
    std::cout << "You selected: " << appModeToString(mode) << std::endl;
    return mode;
}

const char* appModeToString(AppMode mode)
{
    switch (mode)
    {
    case MAIN_SOFTWARE:    return "Main Software";
    case FIRMWARE_UPDATER: return "Firmware Updater";
    }
    return "UNKNOWN";
}
