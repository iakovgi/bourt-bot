#ifndef BOURTBOT_H
#define BOURTBOT_H

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>

#include "TelegramBotAPI.h"

namespace tg = Telegram;

class BourtBot final : public QObject {
    Q_OBJECT

public:
    enum PollOption : qint32 {
        NO = 0,
        YES_FULLTIME = 1
    };

    struct ReceivedAnswer {
        tg::User user{};
        PollOption option{ PollOption::NO };
    };

    using PollId = QString;
    using PollResult = QList<ReceivedAnswer>;
    using CourtId = qint32;

    struct CourtConfig {
        static QVector<QString> const availableCourts;

        QMap<CourtId, qint32> data{};

        [[nodiscard]] auto toString() const -> QString;
    };

public:
    [[nodiscard]] explicit BourtBot( std::shared_ptr<tg::BotSettings> const& botSettings );
    ~BourtBot() override = default;

private slots:
    auto onUpdate() -> void;

    auto onMessageReceived( qint32 updateId, tg::Message message ) -> void;
    auto onPollAnswerReceived( qint32 updateId, tg::PollAnswer pollAnswer ) -> void;
    auto onCallbackQueryReceived( qint32 updateId, tg::CallbackQuery callbackQuery ) -> void;

private:
    auto commandNewPoll( qint64 chatId ) -> void;

    auto queryStop( tg::CallbackQuery const& query ) -> void;
    auto queryAddCourt( tg::CallbackQuery const& query ) -> void;
    auto queryCourtAdded( tg::CallbackQuery const& query ) -> void;
    auto queryResetConfig( tg::CallbackQuery const& query ) -> void;
    auto queryCreateTimetable( tg::CallbackQuery const& query ) -> void;

    static auto getDefaultKeyboardMarkup() -> tg::InlineKeyboardMarkup;

    auto resetState() -> void;

private:
    tg::Bot m_bot{};
    QTimer *m_timer{ nullptr };

    qint32 m_offset{ 0 };

private:
    PollId m_activePollId{};
    CourtConfig m_config{};
    PollResult m_pollResult{};

private:
    static QVector<tg::BotCommand> m_botCommands;
};

#endif// BOURTBOT_H
