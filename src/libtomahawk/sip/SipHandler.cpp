/* === This file is part of Tomahawk Player - <http://tomahawk-player.org> ===
 *
 *   Copyright 2010-2011, Christian Muehlhaeuser <muesli@tomahawk-player.org>
 *
 *   Tomahawk is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Tomahawk is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Tomahawk. If not, see <http://www.gnu.org/licenses/>.
 */

#include "SipHandler.h"
#include "sip/SipPlugin.h"

#include <QCoreApplication>
#include <QDir>
#include <QPluginLoader>
#include <QMessageBox>

#include "functimeout.h"

#include "database/database.h"
#include "network/controlconnection.h"
#include "network/servent.h"
#include "sourcelist.h"
#include "tomahawksettings.h"
#include "utils/logger.h"
#include "accounts/accountmanager.h"

#include "config.h"

SipHandler* SipHandler::s_instance = 0;


SipHandler*
SipHandler::instance()
{
    if ( !s_instance )
        new SipHandler( 0 );

    return s_instance;
}


SipHandler::SipHandler( QObject* parent )
    : QObject( parent )
    , m_connected( false )
{
    s_instance = this;

    connect( TomahawkSettings::instance(), SIGNAL( changed() ), SLOT( onSettingsChanged() ) );
}


SipHandler::~SipHandler()
{
    qDebug() << Q_FUNC_INFO;
    disconnectAll();
    qDeleteAll( m_allPlugins );
}


const QPixmap
SipHandler::avatar( const QString& name ) const
{
//    qDebug() << Q_FUNC_INFO << "Getting avatar" << name; // << m_usernameAvatars.keys();
    if( m_usernameAvatars.contains( name ) )
    {
//        qDebug() << Q_FUNC_INFO << "Getting avatar and avatar != null ";
        Q_ASSERT(!m_usernameAvatars.value( name ).isNull());
        return m_usernameAvatars.value( name );
    }
    else
    {
//        qDebug() << Q_FUNC_INFO << "Getting avatar and avatar == null :-(";
        return QPixmap();
    }
}


const SipInfo
SipHandler::sipInfo( const QString& peerId ) const
{
    return m_peersSipInfos.value( peerId );
}

const QString
SipHandler::versionString( const QString& peerId ) const
{
    return m_peersSoftwareVersions.value( peerId );
}


void
SipHandler::onSettingsChanged()
{
    checkSettings();
}


void
SipHandler::hookUpPlugin( SipPlugin* sip )
{
    QObject::connect( sip, SIGNAL( peerOnline( QString ) ), SLOT( onPeerOnline( QString ) ) );
    QObject::connect( sip, SIGNAL( peerOffline( QString ) ), SLOT( onPeerOffline( QString ) ) );
    QObject::connect( sip, SIGNAL( msgReceived( QString, QString ) ), SLOT( onMessage( QString, QString ) ) );
    QObject::connect( sip, SIGNAL( sipInfoReceived( QString, SipInfo ) ), SLOT( onSipInfo( QString, SipInfo ) ) );
    QObject::connect( sip, SIGNAL( softwareVersionReceived( QString, QString ) ), SLOT( onSoftwareVersion( QString, QString ) ) );

    QObject::connect( sip, SIGNAL( error( int, QString ) ), SLOT( onError( int, QString ) ) );
    QObject::connect( sip, SIGNAL( stateChanged( SipPlugin::ConnectionState ) ), SLOT( onStateChanged( SipPlugin::ConnectionState ) ) );

    QObject::connect( sip, SIGNAL( avatarReceived( QString, QPixmap ) ), SLOT( onAvatarReceived( QString, QPixmap ) ) );
    QObject::connect( sip, SIGNAL( avatarReceived( QPixmap ) ), SLOT( onAvatarReceived( QPixmap ) ) );

    QObject::connect( sip->account(), SIGNAL( configurationChanged() ), sip, SLOT( configurationChanged() ) );
}


bool
SipHandler::pluginLoaded( const QString& pluginId ) const
{
    foreach( SipPlugin* plugin, m_allPlugins )
    {
        if ( plugin->pluginId() == pluginId )
            return true;
    }

    return false;
}


void
SipHandler::checkSettings()
{
    foreach( SipPlugin* sip, m_allPlugins )
    {
        sip->checkSettings();
    }
}


void
SipHandler::addSipPlugin( SipPlugin* p )
{
    m_allPlugins << p;

    hookUpPlugin( p );
    p->connectPlugin();

    emit pluginAdded( p );
}


void
SipHandler::removeSipPlugin( SipPlugin* p )
{
    p->disconnectPlugin();

    emit pluginRemoved( p );

    m_allPlugins.removeAll( p );
}


void
SipHandler::loadFromAccountManager()
{
    QList< Tomahawk::Accounts::Account* > accountList = Tomahawk::Accounts::AccountManager::instance()->getAccounts( Tomahawk::Accounts::SipType );
    foreach( Tomahawk::Accounts::Account* account, accountList )
    {
        SipPlugin* p = account->sipPlugin();
        addSipPlugin( p );
    }
    m_connected = true;
}


void
SipHandler::connectAll()
{
    foreach( SipPlugin* sip, m_allPlugins )
    {
        sip->connectPlugin();
    }
    m_connected = true;
}


void
SipHandler::disconnectAll()
{
    foreach( SipPlugin* p, m_connectedPlugins )
        p->disconnectPlugin();

    SourceList::instance()->removeAllRemote();
    m_connected = false;
}


void
SipHandler::connectPlugin( const QString &pluginId )
{
#ifndef TOMAHAWK_HEADLESS
    if ( !TomahawkSettings::instance()->acceptedLegalWarning() )
    {
        int result = QMessageBox::question(
            //TomahawkApp::instance()->mainWindow(),
            0, tr( "Legal Warning" ),
            tr( "By pressing OK below, you agree that your use of Tomahawk will be in accordance with any applicable laws, including copyright and intellectual property laws, in effect in your country of residence, and indemnify the Tomahawk developers and project from liability should you choose to break those laws.\n\nFor more information, please see http://gettomahawk.com/legal" ),
            tr( "I Do Not Agree" ), tr( "I Agree" )
        );
        if ( result != 1 )
            return;
        else
            TomahawkSettings::instance()->setAcceptedLegalWarning( true );
    }
#endif
    foreach( SipPlugin* sip, m_allPlugins )
    {
        if ( sip->pluginId() == pluginId )
        {
            Q_ASSERT( m_allPlugins.contains( sip ) ); // make sure the plugin we're connecting is enabled. should always be the case
            //each sip should refreshProxy() or take care of that function in some other way during connection
            sip->connectPlugin();
        }
    }
}


void
SipHandler::disconnectPlugin( const QString &pluginName )
{
    foreach( SipPlugin* sip, m_connectedPlugins )
    {
        if ( sip->account()->accountId() == pluginName )
            sip->disconnectPlugin();
    }
}


QList< SipPlugin* >
SipHandler::allPlugins() const
{
    return m_allPlugins;
}


QList< SipPlugin* >
SipHandler::connectedPlugins() const
{
    return m_connectedPlugins;
}


void
SipHandler::toggleConnect()
{
    if( m_connected )
        disconnectAll();
    else
        connectAll();
}


void
SipHandler::refreshProxy()
{
    qDebug() << Q_FUNC_INFO;

    foreach( SipPlugin* sip, m_allPlugins )
        sip->refreshProxy();
}


void
SipHandler::onPeerOnline( const QString& jid )
{
//    qDebug() << Q_FUNC_INFO;
    qDebug() << "SIP online:" << jid;

    SipPlugin* sip = qobject_cast<SipPlugin*>(sender());

    QVariantMap m;
    if( Servent::instance()->visibleExternally() )
    {
        QString key = uuid();
        ControlConnection* conn = new ControlConnection( Servent::instance() );

        const QString& nodeid = Database::instance()->dbid();

        //TODO: this is a terrible assumption, help me clean this up, mighty muesli!
        if ( jid.contains( "@conference.") )
            conn->setName( jid );
        else
            conn->setName( jid.left( jid.indexOf( "/" ) ) );

        conn->setId( nodeid );

        Servent::instance()->registerOffer( key, conn );
        m["visible"] = true;
        m["ip"] = Servent::instance()->externalAddress();
        m["port"] = Servent::instance()->externalPort();
        m["key"] = key;
        m["uniqname"] = nodeid;

        qDebug() << "Asking them to connect to us:" << m;
    }
    else
    {
        m["visible"] = false;
        qDebug() << "We are not visible externally:" << m;
    }

    QJson::Serializer ser;
    QByteArray ba = ser.serialize( m );

    sip->sendMsg( jid, QString::fromAscii( ba ) );
}


void
SipHandler::onPeerOffline( const QString& jid )
{
//    qDebug() << Q_FUNC_INFO;
    qDebug() << "SIP offline:" << jid;
}


void
SipHandler::onSipInfo( const QString& peerId, const SipInfo& info )
{
    qDebug() << Q_FUNC_INFO << "SIP Message:" << peerId << info;

    /*
      If only one party is externally visible, connection is obvious
      If both are, peer with lowest IP address initiates the connection.
      This avoids dupe connections.
     */
    if ( info.isVisible() )
    {
        if( !Servent::instance()->visibleExternally() ||
            Servent::instance()->externalAddress() <= info.host().hostName() )
        {
            qDebug() << "Initiate connection to" << peerId;
            Servent::instance()->connectToPeer( info.host().hostName(),
                                          info.port(),
                                          info.key(),
                                          peerId,
                                          info.uniqname() );
        }
        else
        {
            qDebug() << Q_FUNC_INFO << "They should be conecting to us...";
        }
    }
    else
    {
        qDebug() << Q_FUNC_INFO << "They are not visible, doing nothing atm";
    }

    m_peersSipInfos.insert( peerId, info );
}

void SipHandler::onSoftwareVersion(const QString& peerId, const QString& versionString)
{
    m_peersSoftwareVersions.insert( peerId, versionString );
}

void
SipHandler::onMessage( const QString& from, const QString& msg )
{
    qDebug() << Q_FUNC_INFO << from << msg;
}


void
SipHandler::onError( int code, const QString& msg )
{
    SipPlugin* sip = qobject_cast< SipPlugin* >( sender() );
    Q_ASSERT( sip );

    qWarning() << "Failed to connect to SIP:" << sip->account()->accountFriendlyName() << code << msg;

    if ( code == SipPlugin::AuthError )
    {
        emit authError( sip );
    }
    else
    {
        QTimer::singleShot( 10000, sip, SLOT( connectPlugin() ) );
    }
}


void
SipHandler::onStateChanged( SipPlugin::ConnectionState state )
{
    SipPlugin* sip = qobject_cast< SipPlugin* >( sender() );
    Q_ASSERT( sip );

    if ( sip->connectionState() == SipPlugin::Disconnected )
    {
        m_connectedPlugins.removeAll( sip );
        emit disconnected( sip );
    }
    else if ( sip->connectionState() == SipPlugin::Connected )
    {
        m_connectedPlugins << sip;
        emit connected( sip );
    }

    emit stateChanged( sip, state );
}


void
SipHandler::onAvatarReceived( const QString& from, const QPixmap& avatar )
{
//    qDebug() << Q_FUNC_INFO << "setting avatar on source for" << from;
    if ( avatar.isNull() )
    {
//        qDebug() << Q_FUNC_INFO << "got null pixmap, not adding anything";
        return;
    }

    m_usernameAvatars.insert( from, avatar );

    //

    //Tomahawk::source_ptr source = ->source();
    ControlConnection *conn = Servent::instance()->lookupControlConnection( from );
    if( conn )
    {
//        qDebug() << Q_FUNC_INFO << from << "got control connection";
        Tomahawk::source_ptr source = conn->source();
        if( source )
        {

//            qDebug() << Q_FUNC_INFO << from << "got source, setting avatar";
            source->setAvatar( avatar );
        }
        else
        {
//            qDebug() << Q_FUNC_INFO << from << "no source found, not setting avatar";
        }
    }
    else
    {
//        qDebug() << Q_FUNC_INFO << from << "no control connection setup yet";
    }
}


void
SipHandler::onAvatarReceived( const QPixmap& avatar )
{
//    qDebug() << Q_FUNC_INFO << "Set own avatar on MyCollection";
    SourceList::instance()->getLocal()->setAvatar( avatar );
}
