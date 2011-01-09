#include "databasecommand_createdynamicplaylist.h"

#include <QSqlQuery>
#include <QSqlDriver>

#include "dynamic/DynamicPlaylist.h"
#include "dynamic/DynamicControl.h"
#include "dynamic/GeneratorInterface.h"

#include "network/servent.h"

using namespace Tomahawk;


DatabaseCommand_CreateDynamicPlaylist::DatabaseCommand_CreateDynamicPlaylist( QObject* parent )
: DatabaseCommand_CreatePlaylist( parent )
{
    qDebug() << Q_FUNC_INFO << "creating dynamiccreatecommand 1";
}


DatabaseCommand_CreateDynamicPlaylist::DatabaseCommand_CreateDynamicPlaylist( const source_ptr& author,
                                                                const dynplaylist_ptr& playlist )
    : DatabaseCommand_CreatePlaylist( author, playlist.staticCast<Playlist>() )
    , m_playlist( playlist )
{
    qDebug() << Q_FUNC_INFO << "creating dynamiccreatecommand 2";
}


void
DatabaseCommand_CreateDynamicPlaylist::exec( DatabaseImpl* lib )
{
    qDebug() << Q_FUNC_INFO;
    Q_ASSERT( !m_playlist.isNull() );
    Q_ASSERT( !source().isNull() );
    
    DatabaseCommand_CreatePlaylist::exec( lib );
    qDebug() << "Created normal playlist, now creating additional dynamic info!";
    
    TomahawkSqlQuery cre = lib->newquery();

    cre.prepare( QString( "INSERT INTO dynamic_playlist( guid, pltype, plmode ) "
                 "VALUES( '%1', '%2', %3 )" ).arg( m_playlist->guid() ).arg( m_playlist->type() ).arg( m_playlist->mode() ) );
        
    cre.exec();
    
    // save the controls
    cre = lib->newquery();
    cre.prepare( "INSERT INTO dynamic_playlist_controls( id, selectedType, match, input) "
                 "VALUES( :id, :selectedType, :match, :input )" );
    foreach( const dyncontrol_ptr& control, m_playlist->generator()->controls() ) {
    
        cre.bindValue( ":id", control->id() );
        cre.bindValue( ":selectedType", control->selectedType() );
        cre.bindValue( ":match", control->match() );
        cre.bindValue( ":input", control->input() );
        
        qDebug() << "CREATE DYNPLAYLIST CONTROL:" << cre.boundValues();
        
        cre.exec();
    }
}


void
DatabaseCommand_CreateDynamicPlaylist::postCommitHook()
{
    qDebug() << Q_FUNC_INFO;
    if( report() == false )
        return;
    
    qDebug() << Q_FUNC_INFO << "..reporting..";
    m_playlist->reportCreated( m_playlist );
    
    if( source()->isLocal() )
        Servent::instance()->triggerDBSync();
}
