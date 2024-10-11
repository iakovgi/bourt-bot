#include "BourtBot.h"

#include <format>
#include <chrono>
#include <stdexcept>

#include <QTimer>

using namespace std::literals::chrono_literals;

// TODO: Read from config
QVector<QString> const BourtBot::CourtConfig::availableCourts = {
    "1", "2", "3", "4", "5"
};

// TODO: Read from config
QVector<QString> const BourtBot::CourtConfig::availableCourtTimeSlots = {
    "10:15",
    "11:00",
    "11:45",
    "12:30",
    "13:15",
    "14:00",
    "14:45",
    "15:30",
    "16:15",
    "17:00"
};

QVector<tg::BotCommand> BourtBot::m_botCommands = {
    { "new_poll", "Creates new attendance poll" },
};

QString BourtBot::CourtConfig::toString()
{
    auto text = QString{ "Current court config:\n\n" };

    auto id = TimeSlotId{};
    for ( auto const& timeSlot : data ) {
        if ( !timeSlot.isEmpty() ) {
            text += QString( "Courts at %1: " ).arg( availableCourtTimeSlots[ id ] );
            for ( auto const& courtId : timeSlot )
                text += QString( "%1 " ).arg( availableCourts[ courtId ] );
            text += "\n";
        }
        ++id;
    }

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
    connect( &m_bot, &tg::Bot::errorOccured, this, &BourtBot::onErrorOccurred );
    connect( &m_bot, &tg::Bot::networkErrorOccured, this, &BourtBot::onNetworkErrorOccurred );

    m_bot.setMyCommands( m_botCommands );

    connect( &m_bot, &tg::Bot::messageReceived, this, &BourtBot::onMessageReceived );
    connect( &m_bot, &tg::Bot::callbackQueryReceived, this, &BourtBot::onCallbackQueryReceived );
    connect( &m_bot, &tg::Bot::pollAnswerReceived, this, &BourtBot::onPollAnswerReceived );

    m_timer = new QTimer{ this };
    m_timer->setInterval( 1s );

    connect( m_timer, &QTimer::timeout, this, &BourtBot::onUpdate );
    m_timer->start();
}

void BourtBot::onErrorOccurred( tg::Error error )
{
    qCritical().noquote() << std::format( "Telegram API error [ code = {} ]:\n\tDescription: {}",
        error.error_code,
        error.description.toStdString() );
}

void BourtBot::onNetworkErrorOccurred( tg::Error error )
{
    qCritical().noquote() << std::format( "Network error [ code = {} ]:\n\tDescription: {}",
        error.error_code,
        error.description.toStdString() );
}

void BourtBot::onUpdate()
{
    auto updates = m_bot.getUpdates( m_offset, std::nullopt, 1, std::nullopt );
    for ( auto&& update : updates.get() ) {
        if ( update.update_id >= m_offset )
            m_offset = update.update_id + 1;

        if ( update.message.has_value() )
            emit m_bot.messageReceived( update.update_id, update.message.value() );
        if ( update.callback_query.has_value() )
            emit m_bot.callbackQueryReceived( update.update_id, update.callback_query.value() );
        if ( update.poll_answer.has_value() )
            emit m_bot.pollAnswerReceived( update.update_id, update.poll_answer.value() );
    }
}

void BourtBot::onMessageReceived( qint32 updateId, tg::Message message )
{
    if ( message.text.has_value() && message.text.value().startsWith( "/new_poll" ) )
        return commandNewPoll( message.chat->id );
}

void BourtBot::onPollAnswerReceived( qint32 updateId, tg::PollAnswer pollAnswer )
{
    auto&& pollResult = m_polls[ pollAnswer.poll_id ];

    auto const& user = pollAnswer.user;
    auto it = std::find_if( std::begin( pollResult ), std::end( pollResult ),
    [ &user ]( ReceivedAnswer const& knownAnswer ) {
        return user.id == knownAnswer.user.id;
    } );

    if ( pollAnswer.option_ids.isEmpty() && it != std::cend( pollResult ) ) {
        pollResult.erase( it );
        return;
    }

    auto option = static_cast<PollOption>( pollAnswer.option_ids.first() );

    if ( it == std::cend( pollResult ) ) {
        pollResult.emplace_back( ReceivedAnswer{ .user = pollAnswer.user,
                                                 .option = option } );
        return;
    }

    it->option = option;
}

void BourtBot::onCallbackQueryReceived( qint32 updateId, tg::CallbackQuery callbackQuery )
{
    m_bot.answerCallbackQuery( callbackQuery.id );

    auto const callbackHandlers = QVector{
        QPair{ "/stop", &BourtBot::queryStop },
        QPair{ "/add_court", &BourtBot::queryAddCourt },
        QPair{ "/reset_config", &BourtBot::queryResetConfig },
        QPair{ "/create_timetable", &BourtBot::queryCreateTimetable }
    };

    for ( auto const& [ prefix, handler ] : callbackHandlers ) {
        if ( callbackQuery.data.has_value() && callbackQuery.data->startsWith( prefix ) ) {
            ( this->*handler )( callbackQuery );
        }
    }
}

void BourtBot::queryStop( tg::CallbackQuery const& query )
{
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

void BourtBot::queryAddCourt( tg::CallbackQuery const& query )
{
    auto const& data = query.data.value();
    auto const& message = query.message.value();

    auto pieces = data.split( "@" );

    switch ( std::size( pieces ) ) {
        // Select court
        case 1: {
            auto keyboardMarkup = tg::InlineKeyboardMarkup{};
            keyboardMarkup.inline_keyboard = { {} };

            int i = 0;
            for ( auto const& availableCourt : CourtConfig::availableCourts ) {
                auto button = tg::InlineKeyboardButton{};
                button.text = QString( "#%1" ).arg( availableCourt );
                button.callback_data = QString( "/add_court@%1" ).arg( i );

                auto& buttonList = keyboardMarkup.inline_keyboard.first();
                buttonList.emplace_back( std::move( button ) );

                ++i;
            }

            auto editArgs = tg::FunctionArguments::EditMessageReplyMarkup{
                .chat_id = message.chat->id,
                .message_id = message.message_id,
                .reply_markup = keyboardMarkup
            };
            m_bot.editMessageReplyMarkup( editArgs );

            break;
        }
        // Select timeslot
        case 2: {
            auto keyboardMarkup = tg::InlineKeyboardMarkup{};
            keyboardMarkup.inline_keyboard = {};

            int i = 0;
            for ( auto const& availableCourtTimeSlot : CourtConfig::availableCourtTimeSlots ) {
                auto button = tg::InlineKeyboardButton{};
                button.text = availableCourtTimeSlot;
                button.callback_data = QString( "%1@%2" ).arg( data ).arg( i );

                auto& buttonList = keyboardMarkup.inline_keyboard;
                buttonList.emplace_back( QVector{ std::move( button ) } );

                ++i;
            }

            auto editArgs = tg::FunctionArguments::EditMessageReplyMarkup{
                .chat_id = message.chat->id,
                .message_id = message.message_id,
                .reply_markup = keyboardMarkup
            };
            m_bot.editMessageReplyMarkup( editArgs );

            break;
        }
        // Finalize command
        case 3: {
            CourtConfig::CourtId courtId = pieces[ 1 ].toInt();
            CourtConfig::TimeSlotId timeSlotId = pieces[ 2 ].toInt();

            PollId pollId = message.reply_to_message->get()->poll->id;

            auto& config = m_configs[ pollId ];
            config.data[ timeSlotId ].insert( courtId );

            auto editArgs = tg::FunctionArguments::EditMessageText{
                .chat_id = message.chat->id,
                .text = config.toString(),
                .message_id = message.message_id,
                .reply_markup = getDefaultKeyboardMarkup()
            };
            m_bot.editMessageText( editArgs );

            break;
        }
    }
}

void BourtBot::queryResetConfig( tg::CallbackQuery const& query )
{
    auto const& message = query.message.value();
    PollId pollId = message.reply_to_message->get()->poll->id;

    m_configs[ pollId ] = CourtConfig{};

    auto editArgs = tg::FunctionArguments::EditMessageText{
        .chat_id = message.chat->id,
        .text = "Config was reset.",
        .message_id = message.message_id,
        .reply_markup = getDefaultKeyboardMarkup()
    };
    m_bot.editMessageText( editArgs );
}

void BourtBot::queryCreateTimetable( tg::CallbackQuery const& query )
{
    auto const& message = query.message.value();
    PollId pollId = message.reply_to_message->get()->poll->id;

    auto sendMessageArgs = tg::FunctionArguments::SendMessage{
        .chat_id = message.chat->id,
        .text = m_configs[ pollId ].toString(),
        .reply_to_message_id = message.message_id
    };
    m_bot.sendMessage( sendMessageArgs );
}

tg::InlineKeyboardMarkup BourtBot::getDefaultKeyboardMarkup()
{
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

void BourtBot::commandNewPoll( qint64 chatId )
{
    auto button = tg::InlineKeyboardButton{};
    button.text = "Stop poll and configure courts";
    button.callback_data = "/stop";

    auto inlineKeyboardMarkup = tg::InlineKeyboardMarkup{};
    inlineKeyboardMarkup.inline_keyboard = { { button } };

    auto sendPollArgs = tg::FunctionArguments::SendPoll{
        .chat_id = chatId,
        .question = QString{ "Squash this week?" },
        .options = { "No", "Yes, 45 min", "Yes, 90 min" },
        .is_anonymous = false,
        .reply_markup = inlineKeyboardMarkup
    };
    auto pollMessage = m_bot.sendPoll( sendPollArgs ).get();
}
