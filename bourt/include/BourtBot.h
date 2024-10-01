#ifndef BOURTBOT_H
#define BOURTBOT_H

#include <map>
#include <list>

#include <QObject>
#include <QString>

#include "TelegramBotAPI.h"

namespace tg = Telegram;

class BourtBot final : public QObject {
    Q_OBJECT

public:
    BourtBot( std::shared_ptr<tg::BotSettings> const& botSettings );
    ~BourtBot() override = default;

private slots:
    void onErrorOccured( tg::Error error );
    void onNetworkErrorOccured( tg::Error error );

    void onUpdate();

    void onMessageReceived( qint32 updateId, tg::Message message );
    void onPollAnswerReceived( qint32 updateId, tg::PollAnswer pollAnswer );
    void onCallbackQueryReceived( qint32 updateId, tg::CallbackQuery callbackQuery );

private:
    void commandNewPoll( qint64 chatId );

private:
    tg::Bot m_bot{};
    QTimer *m_timer{ nullptr };

    qint32 m_offset{ 0 };

private:
    std::map<QString, std::list<tg::User>> m_records{};

private:
    static QVector<tg::BotCommand> m_botCommands;
};

#endif// BOURTBOT_H
