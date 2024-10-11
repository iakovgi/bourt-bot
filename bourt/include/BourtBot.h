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
    explicit BourtBot( std::shared_ptr<tg::BotSettings> const& botSettings );
    ~BourtBot() override = default;

private slots:
    void onErrorOccurred( tg::Error error );
    void onNetworkErrorOccurred( tg::Error error );

    void onUpdate();

    void onMessageReceived( qint32 updateId, tg::Message message );
    void onPollAnswerReceived( qint32 updateId, tg::PollAnswer pollAnswer );
    void onCallbackQueryReceived( qint32 updateId, tg::CallbackQuery callbackQuery );

private:
    void commandNewPoll( qint64 chatId );

    void queryStop( tg::CallbackQuery const& query );
    void queryAddCourt( tg::CallbackQuery const& query );
    void queryResetConfig( tg::CallbackQuery const& query );
    void queryCreateTimetable( tg::CallbackQuery const& query );

    static tg::InlineKeyboardMarkup getDefaultKeyboardMarkup();

private:
    tg::Bot m_bot{};
    QTimer *m_timer{ nullptr };

    qint32 m_offset{ 0 };

private:
    enum PollOption : qint32 {
        NO = 0,
        YES_HALFTIME = 1,
        YES_FULLTIME = 2
    };

    struct ReceivedAnswer {
        tg::User user{};
        PollOption option{ PollOption::NO };
    };

    using PollId = QString;
    using PollResult = std::list<ReceivedAnswer>;
    std::map<PollId, PollResult> m_polls{};

    struct CourtConfig {
        static QVector<QString> const availableCourts;
        static QVector<QString> const availableCourtTimeSlots;

        using TimeSlotId = qint32;
        using CourtId = qint32;
        using TimeSlot = QSet<CourtId>;
        QVector<TimeSlot> data{};

        CourtConfig()
        {
            data.resize( std::size( availableCourtTimeSlots ) );
        }

        QString toString();
    };

    std::map<PollId, CourtConfig> m_configs{};

private:
    static QVector<tg::BotCommand> m_botCommands;
};

#endif// BOURTBOT_H
