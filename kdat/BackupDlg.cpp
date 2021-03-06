// $Id: BackupDlg.cpp 465369 2005-09-29 14:33:08Z mueller $
//
// KDat - a tar-based DAT archiver
// Copyright (C) 1998-2000  Sean Vyain, svyain@mail.tds.net
// Copyright (C) 2001-2002  Lawrence Widman, kdat@cardiothink.com
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/errno.h>
#include <sys/stat.h>

#include <qdir.h>
#include <qlabel.h>
#include <qlayout.h>
#include <kpushbutton.h>
#include <kstdguiitem.h>

#include <kapplication.h>
#include <kprocess.h>
#include <kmessagebox.h>
#include <kpushbutton.h>
#include <kstdguiitem.h>

#include "Archive.h"
#include "BackupDlg.h"
#include "LoggerWidget.h"
#include "Options.h"
#include "Tape.h"
#include "TapeDrive.h"
#include "TarParser.h"
#include "Util.h"
#include <klocale.h>

#include "BackupDlg.moc"

BackupDlg::BackupDlg( const QString & archiveName, const QString & workingDir, const QStringList& files, bool oneFilesystem, bool incremental,
                      const QString & snapshot, bool removeSnapshot, int archiveSize, Tape* tape,
                      QWidget* parent, const char* name )
        : QDialog( parent, name, TRUE ),
          _proc( NULL ),
          _tarParser( NULL ),
          _archiveName( archiveName ),
          _workingDir( workingDir ),
          _files(files),
          _oneFilesystem( oneFilesystem ),
          _incremental( incremental ),
          _snapshot( snapshot ),
          _removeSnapshot( removeSnapshot ),
          _archiveSize( archiveSize ),
          _tape( tape ),
          _totalKBytes( 0.0 ),
          _totalRecords( 0 ),
          _startTime( 0 ),
          _archive( NULL ),
          _aborted( FALSE ),
          _numFiles( 0 ),
          _fileSize( -1 ),
          _fileMTime( -1 ),
          _fileStartRecord( -1 )
{
    // Copy the list of files to archive.

    setCaption( i18n( "KDat: Backup" ) );
    setIconText( i18n( "KDat: Backup" ) );

    resize( 515, 300 );

    /* 2002-01-26 LEW: "Time remaining" was cut off in mid-"g" 
       so we'll provide that plus some space beyond it. */
    // const int labelWidth = 96;
    const int labelWidth = 110;

    QFrame* f1 = new QFrame( this );
    f1->setFrameStyle( QFrame::Panel | QFrame::Sunken );

    QFrame* f2 = new QFrame( this );
    f2->setFrameStyle( QFrame::Panel | QFrame::Sunken );

    QLabel* lbl1 = new QLabel( i18n( "Elapsed time:" ), f1 );
    lbl1->setFixedSize( labelWidth, lbl1->sizeHint().height() );

    _elapsedTime = new QLabel( i18n( "00:00:00" ), f1 );
    _elapsedTime->setFixedHeight( _elapsedTime->sizeHint().height() );

    QLabel* lbl2 = new QLabel( i18n( "Time remaining:" ), f2 );
    lbl2->setFixedSize( labelWidth, lbl2->sizeHint().height() );

    _timeRemaining = new QLabel( i18n( "00:00:00" ), f2 );
    _timeRemaining->setFixedHeight( _timeRemaining->sizeHint().height() );

    QLabel* lbl3 = new QLabel( i18n( "Total KB:" ), f1 );
    lbl3->setFixedSize( labelWidth, lbl3->sizeHint().height() );

    QLabel* totalKbytes = new QLabel( Util::kbytesToString( archiveSize ), f1 );
    totalKbytes->setFixedHeight( totalKbytes->sizeHint().height() );

    QLabel* lbl4 = new QLabel( i18n( "KB written:" ), f2 );
    lbl4->setFixedSize( labelWidth, lbl4->sizeHint().height() );

    _kbytesWritten = new QLabel( i18n( "0KB" ), f2 );
    _kbytesWritten->setFixedHeight( _kbytesWritten->sizeHint().height() );

    QLabel* lbl5 = new QLabel( i18n( "Transfer rate:" ), f1 );
    lbl5->setFixedSize( labelWidth, lbl5->sizeHint().height() );

    _transferRate = new QLabel( i18n( "0KB/min" ), f1 );
    _transferRate->setFixedHeight( _transferRate->sizeHint().height() );

    QLabel* lbl6 = new QLabel( i18n( "Files:" ), f2 );
    lbl6->setFixedSize( labelWidth, lbl6->sizeHint().height() );

    _fileCount = new QLabel( i18n( "0" ), f2 );
    _fileCount->setFixedHeight( _fileCount->sizeHint().height() );

    _log = new LoggerWidget( i18n( "Backup log:" ), this );

    _ok = new KPushButton( KStdGuiItem::ok(), this );
    _ok->setFixedSize( 80, _ok->sizeHint().height() );
    connect( _ok, SIGNAL( clicked() ), this, SLOT( slotOK() ) );
    _ok->setEnabled( FALSE );

    _save = new QPushButton( i18n( "Save Log..." ), this );
    _save->setFixedSize( 80, _save->sizeHint().height() );
    connect( _save, SIGNAL( clicked() ), _log, SLOT( save() ) );
    _save->setEnabled( FALSE );

    _abort = new KPushButton( KStdGuiItem::cancel(), this );
    _abort->setFixedSize( 80, _abort->sizeHint().height() );
    connect( _abort, SIGNAL( clicked() ), this, SLOT( slotAbort() ) );

    QVBoxLayout* l1 = new QVBoxLayout( this, 8, 4 );

    QHBoxLayout* l1_1 = new QHBoxLayout();
    l1->addLayout( l1_1 );
    l1_1->addStrut( 3 * lbl1->height() + 16 );
    l1_1->addWidget( f1 );
    l1_1->addWidget( f2 );

    QVBoxLayout* l1_1_1 = new QVBoxLayout( f1, 4, 4 );

    QHBoxLayout* l1_1_1_1 = new QHBoxLayout();
    l1_1_1->addLayout( l1_1_1_1 );
    l1_1_1_1->addWidget( lbl1 );
    l1_1_1_1->addWidget( _elapsedTime, 1 );

    QHBoxLayout* l1_1_1_2 = new QHBoxLayout();
    l1_1_1->addLayout( l1_1_1_2 );
    l1_1_1_2->addWidget( lbl3 );
    l1_1_1_2->addWidget( totalKbytes, 1 );

    QHBoxLayout* l1_1_1_3 = new QHBoxLayout();
    l1_1_1->addLayout( l1_1_1_3 );
    l1_1_1_3->addWidget( lbl5 );
    l1_1_1_3->addWidget( _transferRate, 1 );

    QVBoxLayout* l1_1_2 = new QVBoxLayout( f2, 4, 4 );

    QHBoxLayout* l1_1_2_1 = new QHBoxLayout();
    l1_1_2->addLayout( l1_1_2_1 );
    l1_1_2_1->addWidget( lbl2 );
    l1_1_2_1->addWidget( _timeRemaining, 1 );

    QHBoxLayout* l1_1_2_2 = new QHBoxLayout();
    l1_1_2->addLayout( l1_1_2_2 );
    l1_1_2_2->addWidget( lbl4 );
    l1_1_2_2->addWidget( _kbytesWritten, 1 );

    QHBoxLayout* l1_1_2_3 = new QHBoxLayout();
    l1_1_2->addLayout( l1_1_2_3 );
    l1_1_2_3->addWidget( lbl6 );
    l1_1_2_3->addWidget( _fileCount, 1 );

    l1->addWidget( _log, 1 );

    QHBoxLayout* l1_2 = new QHBoxLayout();
    l1->addLayout( l1_2 );
    l1_2->addStretch( 1 );
    l1_2->addWidget( _ok );
    l1_2->addWidget( _save );
    l1_2->addWidget( _abort );
}

BackupDlg::~BackupDlg()
{
    delete _tarParser;
}

void BackupDlg::show()
{
    bool bGoOn = false;

    _archive = new Archive( _tape, time( NULL ), _archiveName.utf8() );

    chdir( QFile::encodeName(_workingDir) );

    if ( _removeSnapshot ) {
        unlink( QFile::encodeName(_snapshot) );
    }

    _tarParser = new TarParser();
    connect( _tarParser, SIGNAL( sigEntry( const QString &, int, int, int ) ), this, SLOT( slotEntry( const QString &, int, int, int ) ) );

    _proc = new KProcess();
    *_proc << Options::instance()->getTarCommand();
    if ( _oneFilesystem ) {
        *_proc << "-l";
    }
    if ( _incremental ) {
        *_proc << "-g" << _snapshot;
    }
    *_proc << "-Spcf" << "-";

    // Append the list of files to archive.
    if ( _files.count() == 1 && _files.first() == "." ) {
        dev_t device = 0;
        struct stat info;
        if ( _oneFilesystem ) {
            if ( lstat( ".", &info ) == 0 ) {
                device = info.st_dev;
            }
        }

        // Backup all files in current working directory.
        QDir dir;
	//roland
        //QStringList::Iterator i = dir.entryList( QDir::All, QDir::Name | QDir::DirsFirst ).begin();
        QStringList FilesList = dir.entryList( QDir::All, QDir::Name | QDir::DirsFirst );
        QStringList::Iterator i = FilesList.begin();
	//roland
        for ( ; !(*i).isNull() ; ++i ) {
            if ( *i != "." && *i != ".." ) {
                if ( _oneFilesystem ) {
                    if ( lstat( QFile::encodeName(*i), &info ) == 0 )
                    {
                        if ( info.st_dev == device ) {
                            *_proc << *i;
			    bGoOn = true;
                        }
                    }
                } else {
                    *_proc << *i;
		    bGoOn = true;
                }
            }
        }
    } else {
        // Backup listed files only.
      /* 2002-01-28 LEW */
      // printf("Fixing to list the files/dirs to be dumped:\n");
      /* 2002-01-28 LEW */
        for ( QStringList::Iterator it = _files.begin();
              it != _files.end();
              ++it ) {
	  /* 2002-01-28 LEW */
	  // printf("tar argument: \"%s\"\n", (*it).latin1());
	  /* 2002-01-28 LEW */
            *_proc << *it;
	    bGoOn = true;
	}
    }

    if (bGoOn == false) {
        KMessageBox::information(this, i18n("No files to back up. Aborting."));
        slotAbort();
        return;
    }          

    connect( _proc, SIGNAL( processExited( KProcess* ) ), this, SLOT( slotProcessExited( KProcess* ) ) );
    connect( _proc, SIGNAL( receivedStdout( KProcess*, char*, int ) ), this, SLOT( slotStdout( KProcess*, char*, int ) ) );

    startTimer( 1000 );

    _proc->start( KProcess::NotifyOnExit, KProcess::Stdout );

    QDialog::show();
}

void BackupDlg::slotProcessExited( KProcess* )
{
    updateStats();

    _archive->setEndBlock( _totalRecords / ( Options::instance()->getTapeBlockSize() / 512 ) );

    if ( _fileName.length() > 0 ) {
        _archive->addFile( _fileSize, _fileMTime, _fileStartRecord, _totalRecords, _fileName );
        _fileName = QString::null;
    }

    TapeDrive::instance()->close();
    TapeDrive::instance()->open();

    killTimers();
    delete _proc;

    _tape->addChild( _archive );

    _ok->setEnabled( TRUE );
    _ok->setDefault( TRUE );
    _save->setEnabled( TRUE );
    _abort->setEnabled( FALSE );
}

// the KProcess passes the arguments to tar, and tar's output is piped here.
// The output is shown to _tarParser->slotData(), which figures out which files
// are going to tape and their file parameters, and saves these data into
// kdat's archive.  The raw data are then written to tape with write().
// 2002-01-27 LEW
void BackupDlg::slotStdout( KProcess*, char* buf, int len )
{
    // Don't start throughput timer until the first block of data is written.
    if ( _startTime == 0 ) {
        _startTime = time( NULL );
    }

    /* 2002-01-26 LEW */
    // printf("got a line from tar, length %d bytes...\n", len);
    /* 2002-01-26 LEW */

    // Pass the data through the tar parser to extract the file info.
    _tarParser->slotData( buf, len );

    _totalKBytes += (float)len / 1024.0;
    assert( len % 512 == 0 );
    _totalRecords += len / 512;
    if ( TapeDrive::instance()->write( buf, len ) < len ) {
        _log->append( i18n( "*** Write failed, giving up." ) );

        if ( _proc ) {
            _proc->kill();
        }

        slotProcessExited( 0 );
    }
}

void BackupDlg::slotEntry( const QString& name, int size, int mtime, int record )
{
    if ( _fileName.length() > 0 ) {
        _archive->addFile( _fileSize, _fileMTime, _fileStartRecord, record, _fileName );
    }

    /* 2002-01-28 LEW */
    // printf("BackupDlg::slotEntry called with \"%s\", %d bytes\n", name.latin1(), size);
    /* 2002-01-28 LEW */

    _fileName        = name;
    _fileSize        = size;
    _fileMTime       = mtime;
    _fileStartRecord = record;

    QString tmp;
    tmp.setNum( ++_numFiles );
    _fileCount->setText( tmp );
    _log->append( name );
}

void BackupDlg::slotOK()
{
    if ( _aborted ) {
        reject();
    } else {
        accept();
    }
}

void BackupDlg::slotAbort()
{
    killTimers();
    if ( _proc ) {
        _proc->kill();
        delete _proc;
    }
    delete _archive;
    _aborted = TRUE;

    _ok->setEnabled( TRUE );
    _ok->setDefault( TRUE );
    _save->setEnabled( TRUE );
    _abort->setEnabled( FALSE );
}

void BackupDlg::timerEvent( QTimerEvent* )
{
    updateStats();
}

void BackupDlg::updateStats()
{
    if ( _startTime == 0 ) {
        return;
    }

    QString str;

    int elapsed = time( NULL ) - _startTime;
    str = QString::fromUtf8( QCString().sprintf( i18n( "%02d:%02d:%02d" ).utf8().data(), elapsed / 3600, elapsed / 60 % 60, elapsed % 60 ) );
    _elapsedTime->setText( str );

    int remain = 0;
    if ( (int)_totalKBytes > 0 ) {
        remain = (int)(( (float)_archiveSize - _totalKBytes ) * (float)elapsed / _totalKBytes);
    }
    if ( remain < 0 ) {
        remain = 0;
    }
    str = QString::fromUtf8( QCString().sprintf( i18n( "%02d:%02d:%02d" ).utf8().data(), remain / 3600, remain / 60 % 60, remain % 60 ) );
    _timeRemaining->setText( str );

    str = Util::kbytesToString( (int)_totalKBytes );
    _kbytesWritten->setText( str );

    if ( elapsed > 0 ) {
        str = i18n( "%1/min" ).arg(Util::kbytesToString( (int)_totalKBytes *60 / elapsed ) );
        _transferRate->setText( str );
    }
}
