/***************************************************************************

	mainwindow.h

	Main BletchMAME window

***************************************************************************/

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <memory.h>

#include "prefs.h"
#include "client.h"
#include "info.h"
#include "softwarelist.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
class QLineEdit;
QT_END_NAMESPACE

class CollectionViewModel;
class MameVersion;
class SoftwareListViewModel;
class VersionResultEvent;
class ListXmlResultEvent;

// ======================> MainWindow

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	MainWindow(QWidget *parent = nullptr);
	~MainWindow();

	virtual bool event(QEvent *event);

private slots:
	void on_actionAbout_triggered();
	void on_actionExit_triggered();
	void on_actionPaths_triggered();
	void on_actionRefresh_Machine_Info_triggered();
	void on_actionBletchMAME_web_site_triggered();
	void on_tabWidget_currentChanged(int index);

private:
	// status of MAME version checks
	enum class check_mame_info_status
	{
		SUCCESS,			// we've loaded an info DB that matches the expected MAME version
		MAME_NOT_FOUND,		// we can't find the MAME executable
		DB_NEEDS_REBUILD	// we've found MAME, but we must rebuild the database
	};

	class Pauser
	{
	public:
		Pauser(MainWindow &host)
		{
		}
	};

	// variables configured at startup
	std::unique_ptr<Ui::MainWindow>			m_ui;
	Preferences								m_prefs;
	MameClient								m_client;
	CollectionViewModel *					m_machinesViewModel;
	SoftwareListViewModel *					m_softwareListViewModel;

	// information retrieved by -version
	QString									m_mame_version;

	// information retrieved by -listxml
	info::database							m_info_db;

	// other
	software_list_collection				m_software_list_collection;
	QString									m_software_list_collection_machine_name;

	// task notifications
	bool onVersionCompleted(VersionResultEvent &event);
	bool onListXmlCompleted(const ListXmlResultEvent &event);

	// methods
	bool IsMameExecutablePresent() const;
	void InitialCheckMameInfoDatabase();
	check_mame_info_status CheckMameInfoDatabase();
	bool PromptForMameExecutable();
	bool refreshMameInfoDatabase();
	int messageBox(const QString &message, long style = 0, const QString &caption = "");
	bool isMameVersionAtLeast(const MameVersion &version) const;
	void setupSearchBox(QLineEdit &lineEdit, const char *collection_view_desc_name, CollectionViewModel &collectionViewModel);
	void updateSoftwareList();
	const QString &GetMachineListItemText(info::machine machine, long column) const;
};

#endif // MAINWINDOW_H
