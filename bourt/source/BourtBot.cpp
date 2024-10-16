#include "BourtBot.h"

#include <format>
#include <chrono>
#include <stdexcept>

#include <QTimer>
#include <QQueue>

using namespace std::literals::chrono_literals;

// TODO: Read from config
QVector<QString> const BourtBot::CourtConfig::availableCourts = {
    "1", "2", "3", "4", "5"
};

QVector<tg::BotCommand> BourtBot::m_botCommands = {
    { "new_poll", "Creates new attendance poll" },
};

struct Player {
    tg::User user{};
    qint32 playedQuantumAmount{};

    [[nodiscard]] auto toString() const -> QString {
        return user.first_name;
    }
};

struct CourtQuantum {
    BourtBot::CourtId courtId{};
    QVector<Player> players{};

    [[nodiscard]] auto toString() const -> QString {
        auto text = QString{ "Court #%1: " }.arg( BourtBot::CourtConfig::availableCourts[ courtId ] );
        for ( auto const& player : players ) {
            text += player.toString() + ' ';
        }
        return text;
    }
};

struct Quantum {
    QVector<CourtQuantum> courtQuantums{};
    QQueue<Player> relaxingPlayers{};

    [[nodiscard]] auto toString() const -> QString {
        auto text = QString{};

        for ( auto const& courtQuantum : courtQuantums ) {
            text += courtQuantum.toString() + '\n';
        }

        if ( relaxingPlayers.isEmpty() ) {
            return text;
        }

        text += "Relaxing: ";
        for ( auto const& player : relaxingPlayers ) {
            text += player.toString() + ' ';
        }
        text += '\n';

        return text;
    }
};

auto BourtBot::CourtConfig::toString() const -> QString {
    auto text = QString{ "Current court config:\n\n" };

    for ( const auto& [ id, count ] : data.asKeyValueRange() ) {
        text += QString{ "#%1 x %2\n" }.arg( availableCourts[ id ] ).arg( count );
    }
    text += '\n';

    return text;
}

BourtBot::BourtBot( std::shared_ptr<tg::BotSettings> const& botSettings ) :
    QObject{},
    m_bot{ botSettings }
{
    auto botUser = m_bot.getMe().get();
    qDebug().noquote() << std::format( "getMe:\n\tid = {}\n\tis_bot = {}", botUser.id, botUser.is_bot );

    if ( !botUser.is_bot )
        throw std::logic_error( "Telegram API reported is_bot = false" );

    // Error slots
    connect( &m_bot, &tg::Bot::errorOccured, this, []( tg::Error error ) {
        qCritical().noquote() << std::format( "Telegram API error [ code = {} ]:\n\tDescription: {}",
                                              error.error_code,
                                              error.description.toStdString() );
    });

    connect( &m_bot, &tg::Bot::networkErrorOccured, this, []( tg::Error error ) {
        qCritical().noquote() << std::format( "Network error [ code = {} ]:\n\tDescription: {}",
                                              error.error_code,
                                              error.description.toStdString() );
    } );

    m_bot.setMyCommands( m_botCommands );

    connect( &m_bot, &tg::Bot::messageReceived, this, &BourtBot::onMessageReceived );
    connect( &m_bot, &tg::Bot::callbackQueryReceived, this, &BourtBot::onCallbackQueryReceived );
    connect( &m_bot, &tg::Bot::pollAnswerReceived, this, &BourtBot::onPollAnswerReceived );

    m_timer = new QTimer{ this };
    m_timer->setInterval( 1s );

    connect( m_timer, &QTimer::timeout, this, &BourtBot::onUpdate );
    m_timer->start();
}

auto BourtBot::onUpdate() -> void {
    auto updates = m_bot.getUpdates( m_offset, std::nullopt, 1, std::nullopt );
    for ( auto&& update : updates.get() ) {
        if ( update.update_id >= m_offset )
            m_offset = update.update_id + 1;

        try {
            if ( update.message.has_value() )
                    emit m_bot.messageReceived( update.update_id, update.message.value() );
            if ( update.callback_query.has_value() )
                    emit m_bot.callbackQueryReceived( update.update_id, update.callback_query.value() );
            if ( update.poll_answer.has_value() )
                    emit m_bot.pollAnswerReceived( update.update_id, update.poll_answer.value() );
        } catch ( std::exception& e ) {
            qCritical().noquote() << std::format( "Exception swallowed: {}\n", e.what() );
        } catch ( ... ) {
            qCritical().noquote() << std::format( "Unknown exception swallowed\n" );
        }
    }
}

auto BourtBot::onMessageReceived( qint32 updateId, tg::Message message ) -> void {
    if ( message.text.has_value() && message.text.value().startsWith( "/new_poll" ) )
        return commandNewPoll( message.chat->id );
}

auto BourtBot::onPollAnswerReceived( qint32 updateId, tg::PollAnswer pollAnswer ) -> void {
    if ( pollAnswer.poll_id != m_activePollId )
        return;

    auto const& user = pollAnswer.user;
    auto it = std::find_if( std::begin( m_pollResult ), std::end( m_pollResult ),
        [ &user ]( ReceivedAnswer const& knownAnswer ) {
            return user.id == knownAnswer.user.id;
        } );

    if ( pollAnswer.option_ids.isEmpty() && it != std::end( m_pollResult ) ) {
        m_pollResult.erase( it );
        return;
    }

    auto option = static_cast<PollOption>( pollAnswer.option_ids.first() );

    if ( it == std::end( m_pollResult ) ) {
        m_pollResult.emplace_back( ReceivedAnswer{ .user = pollAnswer.user,
                                                   .option = option } );
        return;
    }

    it->option = option;
}

auto BourtBot::onCallbackQueryReceived( qint32 updateId, tg::CallbackQuery callbackQuery ) -> void {
    m_bot.answerCallbackQuery( callbackQuery.id );

    auto const callbackHandlers = QVector{
        QPair{ "/stop", &BourtBot::queryStop },
        QPair{ "/add_court", &BourtBot::queryAddCourt },
        QPair{ "/court_added", &BourtBot::queryCourtAdded },
        QPair{ "/reset_config", &BourtBot::queryResetConfig },
        QPair{ "/create_timetable", &BourtBot::queryCreateTimetable }
    };

    for ( auto const& [ prefix, handler ] : callbackHandlers ) {
        if ( callbackQuery.data.has_value() && callbackQuery.data->startsWith( prefix ) ) {
            ( this->*handler )( callbackQuery );
        }
    }
}

auto BourtBot::queryStop( tg::CallbackQuery const& query ) -> void {
    auto const& message = query.message.value();
    m_bot.stopPoll( message.chat->id, message.message_id );

    auto sendArgs = tg::FunctionArguments::SendMessage{
        .chat_id = message.chat->id,
        .text = "Court configuration needs to be configured",
        .reply_to_message_id = message.message_id,
        .reply_markup = getDefaultKeyboardMarkup()
    };
    m_bot.sendMessage( sendArgs );
}

auto BourtBot::queryAddCourt( tg::CallbackQuery const& query ) -> void {
    auto const& message = query.message.value();

    auto keyboardMarkup = tg::InlineKeyboardMarkup{};
    keyboardMarkup.inline_keyboard = { {} };

    int i = 0;
    for ( auto const& availableCourt : CourtConfig::availableCourts ) {
        auto button = tg::InlineKeyboardButton{};
        button.text = QString( "#%1" ).arg( availableCourt );
        button.callback_data = QString( "/court_added@%1" ).arg( i );

        auto& buttonList = keyboardMarkup.inline_keyboard.back();
        buttonList.emplace_back( std::move( button ) );

        ++i;
    }

    keyboardMarkup.inline_keyboard.emplaceBack( QList<tg::InlineKeyboardButton>{} );
    i = 0;
    for ( auto const& availableCourt : CourtConfig::availableCourts ) {
        auto button = tg::InlineKeyboardButton{};
        button.text = QString( "#%1x2" ).arg( availableCourt );
        button.callback_data = QString( "/court_added@%1@" ).arg( i );

        auto& buttonList = keyboardMarkup.inline_keyboard.back();
        buttonList.emplace_back( std::move( button ) );

        ++i;
    }

    auto editArgs = tg::FunctionArguments::EditMessageReplyMarkup{
        .chat_id = message.chat->id,
        .message_id = message.message_id,
        .reply_markup = keyboardMarkup
    };
    m_bot.editMessageReplyMarkup( editArgs );
}

auto BourtBot::queryCourtAdded( const tg::CallbackQuery& query ) -> void {
    auto const& data = query.data.value();
    auto const& message = query.message.value();

    auto pieces = data.split( "@" );
    CourtId courtId = pieces[ 1 ].toInt();

    m_config.data[ courtId ] += pieces.size() - 1;

    auto editArgs = tg::FunctionArguments::EditMessageText{
        .chat_id = message.chat->id,
        .text = m_config.toString(),
        .message_id = message.message_id,
        .reply_markup = getDefaultKeyboardMarkup()
    };
    m_bot.editMessageText( editArgs );
}

auto BourtBot::queryResetConfig( tg::CallbackQuery const& query ) -> void {
    m_config = CourtConfig{};

    auto const& message = query.message.value();
    auto editArgs = tg::FunctionArguments::EditMessageText{
        .chat_id = message.chat->id,
        .text = "Config was reset.",
        .message_id = message.message_id,
        .reply_markup = getDefaultKeyboardMarkup()
    };
    m_bot.editMessageText( editArgs );
}

auto BourtBot::queryCreateTimetable( tg::CallbackQuery const& query ) -> void {
    constexpr auto maxConsecutiveQuantums = qint32{ 2 };
    constexpr auto timeQuantumsPerCourt = qint32{ 3 };

    auto config = m_config;

    auto maxCourtBookings = qint32{};
    for ( auto const& count : config.data ) {
        maxCourtBookings = std::max( maxCourtBookings, count );
    }
    auto quantumAmount = maxCourtBookings * timeQuantumsPerCourt;

    auto prevQuantum = Quantum{};
    for ( auto const& answer : m_pollResult ) {
        if ( answer.option == PollOption::NO ) {
            continue;
        }

        prevQuantum.relaxingPlayers.enqueue( Player{
            .user = answer.user,
            .playedQuantumAmount = 0,
        } );
    }

    auto timetable = QVector<Quantum>{};
    timetable.reserve( quantumAmount );

    for ( auto quantIdx = 0; quantIdx < quantumAmount; ++quantIdx ) {
        auto readyPlayers = QQueue<Player>{};
        auto relaxingPlayers = QQueue<Player>{};

        for ( auto const& courtQuantum : prevQuantum.courtQuantums ) {
            for ( auto const& player : courtQuantum.players ) {
                if ( player.playedQuantumAmount + 1 > maxConsecutiveQuantums ) {
                    relaxingPlayers.emplaceBack( Player{
                        .user = player.user,
                        .playedQuantumAmount = player.playedQuantumAmount
                    } );
                } else {
                    readyPlayers.emplaceBack( Player{
                        .user = player.user,
                        .playedQuantumAmount = player.playedQuantumAmount
                    } );
                }
            }
        }

        for ( auto const& player : prevQuantum.relaxingPlayers ) {
            readyPlayers.emplaceFront( Player{
                .user = player.user,
                .playedQuantumAmount = 0
            } );
        }

        auto getNextPlayer = [ & ]() -> Player {
            auto player = readyPlayers.isEmpty() ? relaxingPlayers.dequeue() : readyPlayers.dequeue();
            player.playedQuantumAmount++;
            return player;
        };

        auto quantum = Quantum{};
        for ( auto const& [ courtId, count ] : config.data.asKeyValueRange() ) {
            auto courtQuantum = CourtQuantum{
                .courtId = courtId
            };

            courtQuantum.players.emplaceBack( getNextPlayer() );
            courtQuantum.players.emplaceBack( getNextPlayer() );

            quantum.courtQuantums.emplace_back( std::move( courtQuantum ) );
        }

        quantum.relaxingPlayers = std::move( relaxingPlayers );
        while ( !readyPlayers.isEmpty() ) {
            quantum.relaxingPlayers.emplaceFront( readyPlayers.dequeue() );
        }

        prevQuantum = quantum;
        timetable.emplace_back( std::move( quantum ) );

        if ( ( quantIdx + 1 ) % timeQuantumsPerCourt == 0 ) {
            for ( auto&& [ courtId, count ] : config.data.asKeyValueRange() ) {
                count = count - 1;
            }
            erase_if( config.data, []( auto it ) {
                return *it == 0;
            } );
        }
    }

    auto i = quint32{ 0 };
    auto text = QString{ "Timetable: \n\n" };
    for ( auto const& quantum : timetable ) {
        ++i;

        text += QString( "Quantum %1:\n" ).arg( i );
        text += quantum.toString();
        text += "\n------------\n";
    }

    auto const& message = query.message.value();
    auto sendMessageArgs = tg::FunctionArguments::SendMessage{
        .chat_id = message.chat->id,
        .text = text,
        .reply_to_message_id = message.message_id
    };
    m_bot.sendMessage( sendMessageArgs );
}

auto BourtBot::getDefaultKeyboardMarkup() -> tg::InlineKeyboardMarkup {
    auto addCourtButton = tg::InlineKeyboardButton{};
    addCourtButton.text = "Add court";
    addCourtButton.callback_data = "/add_court";

    auto resetConfigButton = tg::InlineKeyboardButton{};
    resetConfigButton.text = "Reset config";
    resetConfigButton.callback_data = "/reset_config";

    auto createTimetableButton = tg::InlineKeyboardButton{};
    createTimetableButton.text = "Create timetable";
    createTimetableButton.callback_data = "/create_timetable";

    auto markup = tg::InlineKeyboardMarkup{};
    markup.inline_keyboard = { { addCourtButton },
                               { resetConfigButton },
                               { createTimetableButton } };

    return markup;
}

auto BourtBot::commandNewPoll( qint64 chatId ) -> void {
    auto button = tg::InlineKeyboardButton{};
    button.text = "Stop poll and configure courts";
    button.callback_data = "/stop";

    auto inlineKeyboardMarkup = tg::InlineKeyboardMarkup{};
    inlineKeyboardMarkup.inline_keyboard = { { button } };

    auto sendPollArgs = tg::FunctionArguments::SendPoll{
        .chat_id = chatId,
        .question = QString{ "Squash this week?" },
        .options = { "No", "Yes" },
        .is_anonymous = false,
        .reply_markup = inlineKeyboardMarkup
    };
    auto pollMessage = m_bot.sendPoll( sendPollArgs ).get();

    m_activePollId = pollMessage.poll->id;
    m_config = CourtConfig{};
    m_pollResult = PollResult{};
}

auto BourtBot::resetState() -> void {
    m_activePollId = PollId{};
    m_config = CourtConfig{};
    m_pollResult = PollResult{};
}
