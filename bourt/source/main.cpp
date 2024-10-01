#include "qcoreapplication.h"

#include "TelegramBotAPI.h"

#include "BourtBot.h"

namespace tg = Telegram;

int main( int argc, char* argv[] )
{
    QCoreApplication coreApplication( argc, argv );

    // Will initialize bot with settings from the file "BotSettings.json" located in the project root directory
    auto settingsFromFile = tg::BotSettings::makeFromFile();

    // Then, using any of these BotSettings, initialize your bot
    auto bourtBot = BourtBot{ settingsFromFile };

    return coreApplication.exec();
}
