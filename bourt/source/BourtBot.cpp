#include "BourtBot.h"

#include <format>
#include <chrono>
#include <stdexcept>

#include <QTimer>

using namespace std::literals::chrono_literals;

QVector<tg::BotCommand> BourtBot::m_botCommands = {
    { "new_poll", "Creates new attendance poll" },
};

BourtBot::BourtBot( std::shared_ptr<tg::BotSettings> const& botSettings ) :
    QObject{},
    m_bot{ botSettings }
{
    auto botUser = m_bot.getMe().get();
    qDebug().noquote() << std::format( "getMe:\n\tid = {}\n\tis_bot = {}", botUser.id, botUser.is_bot );

    if ( !botUser.is_bot )
        throw std::logic_error( "Telegram API reported is_bot = false" );

    // Error slots
    connect( &m_bot, &tg::Bot::errorOccured, this, &BourtBot::onErrorOccured );
    connect( &m_bot, &tg::Bot::networkErrorOccured, this, &BourtBot::onNetworkErrorOccured );

    m_bot.setMyCommands( m_botCommands );

    connect( &m_bot, &tg::Bot::messageReceived, this, &BourtBot::onMessageReceived );
    connect( &m_bot, &tg::Bot::callbackQueryReceived, this, &BourtBot::onCallbackQueryReceived );
    connect( &m_bot, &tg::Bot::pollAnswerReceived, this, &BourtBot::onPollAnswerReceived );

    m_timer = new QTimer{ this };
    m_timer->setInterval( 1s );

    connect( m_timer, &QTimer::timeout, this, &BourtBot::onUpdate );
    m_timer->start();
}

void BourtBot::onErrorOccured( tg::Error error )
{
    qCritical().noquote() << std::format( "Telegram API error [ code = {} ]:\n\tDescription: {}",
        error.error_code,
        error.description.toStdString() );
}

void BourtBot::onNetworkErrorOccured( tg::Error error )
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
    if ( message.text.has_value() && message.text.value() == "/new_poll" )
        return commandNewPoll( message.chat->id );
}

void BourtBot::onPollAnswerReceived( qint32 updateId, tg::PollAnswer pollAnswer )
{
    auto&& record = m_records[ pollAnswer.poll_id ];

    auto&& user = pollAnswer.user;
    auto it = std::find_if( std::begin( record ), std::end( record ), [ &user ]( tg::User const& knownUser ) {
        return user.id == knownUser.id;
    } );

    if ( pollAnswer.option_ids.empty() || !!pollAnswer.option_ids.front() == false ) {
        if ( it != std::end( record ) )
            record.erase( it );

        return;
    }

    if ( !pollAnswer.option_ids.empty() && !!pollAnswer.option_ids.front() == true ) {
        if ( it == std::end( record ) )
            record.push_back( user );

        return;
    }
}

void BourtBot::onCallbackQueryReceived( qint32 updateId, tg::CallbackQuery callbackQuery )
{
    qDebug().noquote() << std::format( "CallbackQuery received:\n\tid = {}", callbackQuery.id.toStdString() );
    m_bot.answerCallbackQuery( callbackQuery.id );

    if ( callbackQuery.message.has_value() ) {
        auto&& message = callbackQuery.message.value();

        if ( message.poll.has_value() ) {
            auto&& poll = message.poll.value();
            auto&& record = m_records[ poll.id ];

            for ( auto&& user : record ) {
                qDebug().noquote() << std::format( "user: {}", user.username.value_or( "Error username!" ).toStdString() );
            }
        }

        m_bot.sendMessage( message.chat->id, "Callback!" );
    }
}

void BourtBot::commandNewPoll( qint64 chatId )
{
    auto inlineKeyboardButton = tg::InlineKeyboardButton{};
    inlineKeyboardButton.text = "Create timetable";
    inlineKeyboardButton.callback_data = "/create_timetable";

    auto inlineKeyboardMarkup = tg::InlineKeyboardMarkup{};
    inlineKeyboardMarkup.inline_keyboard = { { inlineKeyboardButton } };

    auto sendPollArgs = tg::FunctionArguments::SendPoll{
        .chat_id = chatId,
        .question = QString{ "Squash this week?" },
        .options = { "No", "Yes" },
        .is_anonymous = false,
        .reply_markup = inlineKeyboardMarkup
    };
    m_bot.sendPoll( sendPollArgs );
}
