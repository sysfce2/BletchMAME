/***************************************************************************

	mainwindow.cpp

	Main BletchMAME window

***************************************************************************/

#include <QThread>
#include <QMessageBox>
#include <QTimer>
#include <QStringListModel>
#include <QDesktopServices>
#include <QDir>
#include <QUrl>
#include <QCloseEvent>

#include "mainwindow.h"
#include "mameversion.h"
#include "ui_mainwindow.h"
#include "collectionviewmodel.h"
#include "softlistviewmodel.h"
#include "listxmltask.h"
#include "runmachinetask.h"
#include "versiontask.h"
#include "utility.h"
#include "dialogs/about.h"
#include "dialogs/loading.h"
#include "dialogs/paths.h"


//**************************************************************************
//  CONSTANTS
//**************************************************************************

// BletchMAME requires MAME 0.213 or later
const MameVersion REQUIRED_MAME_VERSION = MameVersion(0, 213, false);

// profiles are not yet implemented
#define HAVE_PROFILES		0


//**************************************************************************
//  VERSION INFO
//**************************************************************************

#ifdef _MSC_VER
// we're not supporing build numbers for MSVC builds
static const char build_version[] = "MSVC";
static const char build_revision[] = "MSVC";
static const char build_date_time[] = "MSVC";
#else
extern const char build_version[];
extern const char build_revision[];
extern const char build_date_time[];
#endif


//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// ======================> Pauser

class MainWindow::Pauser
{
public:
	Pauser(MainWindow &host, bool actually_pause = true)
		: m_host(host)
		, m_last_pauser(host.m_current_pauser)
	{
		// if we're running and not pause, pause while the message box is up
		m_is_running = actually_pause && m_host.m_state.has_value() && !m_host.m_state->paused().get();
		if (m_is_running)
			m_host.ChangePaused(true);

		// track the chain of pausers
		m_host.m_current_pauser = this;
	}

	~Pauser()
	{
		if (m_is_running)
			m_host.ChangePaused(false);
		m_host.m_current_pauser = m_last_pauser;
	}

private:
	MainWindow &	m_host;
	const Pauser *	m_last_pauser;
	bool			m_is_running;
};


// ======================> ActionAspect

template<typename TStartAction, typename TStopAction>
class MainWindow::ActionAspect : public Aspect
{
public:
	ActionAspect(TStartAction &&startAction, TStopAction &&stopAction)
		: m_startAction(startAction)
		, m_stopAction(stopAction)
	{
	}

	virtual void start()
	{
		m_startAction();
	}

	virtual void stop()
	{
		m_stopAction();
	}

private:
	TStartAction	m_startAction;
	TStopAction		m_stopAction;
};


// ======================> PropertySyncAspect

template<typename TValueType, typename TObserve>
class MainWindow::PropertySyncAspect : public Aspect
{
public:
	PropertySyncAspect(std::function<void(const TValueType &)> &&setFunc, TValueType &&defaultValue, TObserve &&observeFunc)
		: m_setFunc(std::move(setFunc))
		, m_defaultValue(std::move(defaultValue))
		, m_observeFunc(std::move(observeFunc))
	{
	}

	virtual void start()
	{
		// emplace the observable value into our optional memeber
		m_value.emplace(m_observeFunc());

		// subscribe to changes
		m_value.value().subscribe([this](const TValueType &value) { m_setFunc(value); });

		// and set the initial value
		m_setFunc(m_value.value().get());
	}

	virtual void stop()
	{
		// clear out our value
		m_value.reset();

		// and restore the default
		m_setFunc(m_defaultValue);
	}

private:
	std::function<void(const TValueType &)>			m_setFunc;
	TValueType										m_defaultValue;
	TObserve										m_observeFunc;
	std::optional<observable::value<TValueType>>	m_value;
};


// ======================> StatusBarAspect

class MainWindow::StatusBarAspect : public Aspect
{
public:
	StatusBarAspect(MainWindow &host)
		: m_host(host)
	{
	}

	virtual void start()
	{
		m_host.m_state->phase().subscribe([this]() { update(); });
		m_host.m_state->speed_percent().subscribe([this]() { update(); });
		m_host.m_state->effective_frameskip().subscribe([this]() { update(); });
		m_host.m_state->startup_text().subscribe([this]() { update(); });
		m_host.m_state->images().subscribe([this]() { update(); });
		update();
	}

	virtual void stop()
	{
		update();
	}

private:
	MainWindow &m_host;

	void update()
	{
		// prepare a vector with the status text
		QStringList statusText;

		// is there a running emulation?
		if (state().has_value())
		{
			// first entry depends on whether we are running
			if (state()->phase().get() == status::machine_phase::RUNNING)
			{
				QString speedText;
				int speedPercent = (int)(m_host.m_state->speed_percent().get() * 100.0 + 0.5);
				if (state()->effective_frameskip().get() == 0)
				{
					speedText = QString("%2%1").arg(
						"%",
						QString::number(speedPercent));
				}
				else
				{
					speedText = QString("%2%1 (frameskip %3/10)").arg(
						"%",
						QString::number(speedPercent),
						QString::number((int)m_host.m_state->effective_frameskip().get()));
				}
				statusText.push_back(std::move(speedText));
			}
			else
			{
				statusText.push_back(state()->startup_text().get());
			}

			// next entries come from device displays
			for (auto iter = state()->images().get().cbegin(); iter < state()->images().get().cend(); iter++)
			{
				if (!iter->m_display.isEmpty())
					statusText.push_back(iter->m_display);
			}
		}

		// and specify it
		QString statusTextString = statusText.join(' ');
		statusBar().showMessage(statusTextString);
	}


	std::optional<status::state> &state()
	{
		return m_host.m_state;
	}


	QStatusBar &statusBar()
	{
		return *m_host.m_ui->statusBar;
	}
};


// ======================> MenuBarAspect

class MainWindow::MenuBarAspect : public Aspect
{
public:
	MenuBarAspect(MainWindow &host)
		: m_host(host)
	{
		QMenuBar &menuBar = *m_host.m_ui->menubar;
		m_menuBarShown = true;
		m_menuBarShown.subscribe([&menuBar](bool shown) { menuBar.setVisible(shown); });
	}

	virtual void start()
	{
		// update the menu bar from the prefs
		m_menuBarShown = m_host.m_prefs.GetMenuBarShown();

		// mouse capturing is a bit more involved
		m_mouseCaptured = observable::observe(m_host.m_state->has_input_using_mouse() && !m_menuBarShown);
		m_mouseCaptured.subscribe([this]()
		{
			m_host.Issue({ "SET_MOUSE_ENABLED", m_mouseCaptured ? "true" : "false" });
			// TODO - change cursor?
		});
	}

	virtual void stop()
	{
		m_host.m_prefs.SetMenuBarShown(m_menuBarShown.get());
		m_menuBarShown = true;
	}

private:
	MainWindow &			m_host;
	observable::value<bool>	m_menuBarShown;
	observable::value<bool>	m_mouseCaptured;
};


//**************************************************************************
//  MAIN IMPLEMENTATION
//**************************************************************************

const float MainWindow::s_throttle_rates[] = { 10.0f, 5.0f, 2.0f, 1.0f, 0.5f, 0.2f, 0.1f };

static const CollectionViewDesc s_machine_collection_view_desc =
{
	"machine",
	"name",
	{
		{ "name",			"Name",			85 },
		{ "description",	"Description",		370 },
		{ "year",			"Year",			50 },
		{ "manufacturer",	"Manufacturer",	320 }
	}
};


static const int SOUND_ATTENUATION_OFF = -32;
static const int SOUND_ATTENUATION_ON = 0;


//-------------------------------------------------
//  ctor
//-------------------------------------------------

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent)
	, m_client(*this, m_prefs)
	, m_machinesViewModel(nullptr)
	, m_softwareListViewModel(nullptr)
	, m_menu_bar_shown(false)
	, m_capture_mouse(false)
	, m_pinging(false)
	, m_current_pauser(nullptr)
{
	// set up Qt form
	m_ui = std::make_unique<Ui::MainWindow>();
	m_ui->setupUi(this);

	// initial preferences read
	m_prefs.Load();

	// set up machines view
	m_machinesViewModel = new CollectionViewModel(
		*m_ui->machinesTableView,
		m_prefs,
		s_machine_collection_view_desc,
		[this](long item, long column) -> const QString &{ return GetMachineListItemText(m_info_db.machines()[item], column); },
		[this]() { return m_info_db.machines().size(); },
		false);
	m_info_db.set_on_changed([this]{ m_machinesViewModel->updateListView(); });

	// set up machines search box
	setupSearchBox(*m_ui->machinesSearchBox, "machine", *m_machinesViewModel);

	// set up software list view
	m_softwareListViewModel = new SoftwareListViewModel(
		*m_ui->softwareTableView,
		m_prefs);

	// set up software list search box
	setupSearchBox(*m_ui->softwareSearchBox, SOFTLIST_VIEW_DESC_NAME, *m_softwareListViewModel);

	// set up the ping timer
	QTimer &pingTimer = *new QTimer(this);
	connect(&pingTimer, &QTimer::timeout, this, &MainWindow::InvokePing);
	setupActionAspect([&pingTimer]() { pingTimer.start(500); }, [&pingTimer]() { pingTimer.stop(); });

	// setup properties that pertain to runtime behavior
	setupPropSyncAspect((QWidget &) *m_ui->tabWidget,	&QWidget::isEnabled,	&QWidget::setEnabled,		false);
	setupPropSyncAspect(*m_ui->rootWidget,				&QWidget::isVisible,	&QWidget::setVisible,		[this]() { return AttachToRootPanel(); });
	setupPropSyncAspect((QWidget &) *this,				&QWidget::windowTitle,	&QWidget::setWindowTitle,	[this]() { return observeTitleBarText(); });

	// actions
	setupPropSyncAspect(*m_ui->actionStop,				&QAction::isEnabled,	&QAction::setEnabled,		true);
	setupPropSyncAspect(*m_ui->actionPause,				&QAction::isEnabled,	&QAction::setEnabled,		true);
	setupPropSyncAspect(*m_ui->actionPause,				&QAction::isChecked,	&QAction::setChecked,		[this]() { return observable::observe(m_state->paused()); });
	setupPropSyncAspect(*m_ui->actionDebugger,			&QAction::isEnabled,	&QAction::setEnabled,		true);
	setupPropSyncAspect(*m_ui->actionSoftReset,			&QAction::isEnabled,	&QAction::setEnabled,		true);
	setupPropSyncAspect(*m_ui->actionHardReset,			&QAction::isEnabled,	&QAction::setEnabled,		true);
	setupPropSyncAspect(*m_ui->actionIncreaseSpeed,		&QAction::isEnabled,	&QAction::setEnabled,		true);
	setupPropSyncAspect(*m_ui->actionDecreaseSpeed,		&QAction::isEnabled,	&QAction::setEnabled,		true);
	setupPropSyncAspect(*m_ui->actionWarpMode,			&QAction::isEnabled,	&QAction::setEnabled,		true);
	setupPropSyncAspect(*m_ui->actionToggleSound,		&QAction::isEnabled,	&QAction::setEnabled,		true);
	setupPropSyncAspect(*m_ui->actionToggleSound,		&QAction::isChecked,	&QAction::setChecked,		[this]() { return observable::observe(m_state->sound_attenuation() != SOUND_ATTENUATION_OFF); });

	// special setup for throttle dynamic menu
	QAction &throttleSeparator = *m_ui->menuThrottle->actions()[0];
	for (size_t i = 0; i < sizeof(s_throttle_rates) / sizeof(s_throttle_rates[0]); i++)
	{
		float throttle_rate = s_throttle_rates[i];
		QString text = QString::number((int)(throttle_rate * 100)) + "%";
		QAction &action = *new QAction(text, m_ui->menuThrottle);
		m_ui->menuThrottle->insertAction(&throttleSeparator, &action);
		action.setEnabled(false);
		action.setCheckable(true);

		connect(&action, &QAction::triggered, this, [this, throttle_rate]() { ChangeThrottleRate(throttle_rate); });
		setupPropSyncAspect(action, &QAction::isEnabled, &QAction::setEnabled, true);
		setupPropSyncAspect(action, &QAction::isChecked, &QAction::setChecked, [this, throttle_rate]() { return observable::observe(m_state->throttle_rate() == throttle_rate); });
	}

	// special setup for frameskip dynamic menu
	for (int i = -1; i <= 10; i++)
	{
		QString value = i == -1 ? "auto" : QString::number(i);
		QString text = i == -1 ? "Auto" : QString::number(i);
		QAction &action = *m_ui->menuFrameSkip->addAction(text);
		action.setEnabled(false);
		action.setCheckable(true);

		connect(&action, &QAction::triggered, this, [this, value{ value.toStdString()}]() { Issue({ "frameskip", value }); });
		setupPropSyncAspect(action, &QAction::isEnabled, &QAction::setEnabled, true);
		setupPropSyncAspect(action, &QAction::isChecked, &QAction::setChecked, [this, value{std::move(value)}]() { return observable::observe(m_state->frameskip() == value); });
	}

	// set up the tab widget
	m_ui->tabWidget->setCurrentIndex(static_cast<int>(m_prefs.GetSelectedTab()));

	// set up the aspect controlling status bar behavior
	Aspect::ptr statusBarAspect = std::make_unique<StatusBarAspect>(*this);
	m_aspects.push_back(std::move(statusBarAspect));

	// set up the aspect controlling menu bar behavior
	Aspect::ptr menuBarAspect = std::make_unique<MenuBarAspect>(*this);
	m_aspects.push_back(std::move(menuBarAspect));

	// time for the initial check
	InitialCheckMameInfoDatabase();
}


//-------------------------------------------------
//  dtor
//-------------------------------------------------

MainWindow::~MainWindow()
{
	m_prefs.Save();
}


//-------------------------------------------------
//  setupActionAspect
//-------------------------------------------------

template<typename TStartAction, typename TStopAction>			
void MainWindow::setupActionAspect(TStartAction &&startAction, TStopAction &&stopAction)
{
	Aspect::ptr action = std::make_unique<ActionAspect<TStartAction, TStopAction>>(
		std::move(startAction),
		std::move(stopAction));
	m_aspects.push_back(std::move(action));
}


//-------------------------------------------------
//  setupPropSyncAspect
//-------------------------------------------------

template<typename TObj, typename TValueType>
void MainWindow::setupPropSyncAspect(TObj &obj, TValueType(TObj:: *getFunc)() const, void (TObj::*setFunc)(TValueType), TValueType value)
{
	setupPropSyncAspect(obj, getFunc, setFunc, [value]() { return observable::value<TValueType>(value); });
}


template<typename TObj, typename TValueType, typename TObserve>
void MainWindow::setupPropSyncAspect(TObj &obj, TValueType(TObj::*getFunc)() const, void (TObj::*setFunc)(TValueType), TObserve &&func)
{
	// determine the original value (to be restored when the emulation completes)
	TValueType originalValue = ((obj).*(getFunc))();

	// create the action
	Aspect::ptr action = std::make_unique<PropertySyncAspect<TValueType, TObserve>>(
		[&obj, setFunc](const TValueType &value) { ((obj).*(setFunc))(value); },
		std::move(originalValue),
		std::move(func));

	// and add it to the list
	m_aspects.push_back(std::move(action));
}


template<typename TObj, typename TValueType>
void MainWindow::setupPropSyncAspect(TObj &obj, TValueType(TObj:: *getFunc)() const, void (TObj::*setFunc)(const TValueType &), TValueType value)
{
	setupPropSyncAspect(obj, getFunc, setFunc, [value]() { return observable::value<TValueType>(value); });
}

template<typename TObj, typename TValueType, typename TObserve>
void MainWindow::setupPropSyncAspect(TObj &obj, TValueType(TObj:: *getFunc)() const, void (TObj::*setFunc)(const TValueType &), TObserve &&func)
{
	// determine the original value (to be restored when the emulation completes)
	TValueType originalValue = ((obj).*(getFunc))();

	// create the action
	Aspect::ptr action = std::make_unique<PropertySyncAspect<TValueType, TObserve>>(
		[&obj, setFunc](const TValueType &value) { ((obj).*(setFunc))(value); },
		std::move(originalValue),
		std::move(func));

	// and add it to the list
	m_aspects.push_back(std::move(action));
}


//-------------------------------------------------
//  on_actionStop_triggered
//-------------------------------------------------

void MainWindow::on_actionStop_triggered()
{
	if (shouldPromptOnStop())
	{
		QString message = "Do you really want to stop?\n"
			"\n"
			"All data in emulated RAM will be lost";

		if (messageBox(message, QMessageBox::Yes | QMessageBox::No) != QMessageBox::StandardButton::Yes)
			return;
	}

	InvokeExit();
}


//-------------------------------------------------
//  on_actionPause_triggered
//-------------------------------------------------

void MainWindow::on_actionPause_triggered()
{
	ChangePaused(!m_state->paused().get());
}


//-------------------------------------------------
//  on_actionDebugger_triggered
//-------------------------------------------------

void MainWindow::on_actionDebugger_triggered()
{
	Issue("debugger");
}


//-------------------------------------------------
//  on_actionSoftReset_triggered
//-------------------------------------------------

void MainWindow::on_actionSoftReset_triggered()
{
	Issue("soft_reset");
}


//-------------------------------------------------
//  on_actionHard_Reset_triggered
//-------------------------------------------------

void MainWindow::on_actionHardReset_triggered()
{
	Issue("hard_reset");
}


//-------------------------------------------------
//  on_actionExit_triggered
//-------------------------------------------------

void MainWindow::on_actionExit_triggered()
{
	close();
}


//-------------------------------------------------
//  on_actionIncreaseSpeed_triggered
//-------------------------------------------------

void MainWindow::on_actionIncreaseSpeed_triggered()
{
	ChangeThrottleRate(-1);
}


//-------------------------------------------------
//  on_actionDecreaseSpeed_triggered
//-------------------------------------------------

void MainWindow::on_actionDecreaseSpeed_triggered()
{
	ChangeThrottleRate(+1);
}


//-------------------------------------------------
//  on_actionWarpMode_triggered
//-------------------------------------------------

void MainWindow::on_actionWarpMode_triggered()
{
	ChangeThrottled(!m_state->throttled());
}


//-------------------------------------------------
//  on_actionToggleSound_triggered
//-------------------------------------------------

void MainWindow::on_actionToggleSound_triggered()
{
	bool isEnabled = m_ui->actionToggleSound->isEnabled();
	ChangeSound(!isEnabled);
}


//-------------------------------------------------
//  on_actionPaths_triggered
//-------------------------------------------------

void MainWindow::on_actionPaths_triggered()
{
	std::vector<Preferences::global_path_type> changed_paths;

	// show the dialog
	{
		Pauser pauser(*this);
		PathsDialog dialog(*this, m_prefs);
		dialog.exec();
		if (dialog.result() == QDialog::DialogCode::Accepted)
		{
			changed_paths = dialog.persist();
			m_prefs.Save();
		}
	}

	// lambda to simplify "is this path changed?"
	auto is_changed = [&changed_paths](Preferences::global_path_type type) -> bool
	{
		auto iter = std::find(changed_paths.begin(), changed_paths.end(), type);
		return iter != changed_paths.end();
	};

	// did the user change the executable path?
	if (is_changed(Preferences::global_path_type::EMU_EXECUTABLE))
	{
		// they did; check the MAME info DB
		check_mame_info_status status = CheckMameInfoDatabase();
		switch (status)
		{
		case check_mame_info_status::SUCCESS:
			// we're good!
			break;

		case check_mame_info_status::MAME_NOT_FOUND:
		case check_mame_info_status::DB_NEEDS_REBUILD:
			// in both of these scenarios, we need to clear out the list
			m_info_db.reset();

			// start a rebuild if that is the only problem
			if (status == check_mame_info_status::DB_NEEDS_REBUILD)
				refreshMameInfoDatabase();
			break;

		default:
			throw false;
		}
	}

#if HAVE_PROFILES
	// did the user change the profiles path?
	if (is_changed(Preferences::global_path_type::PROFILES))
		UpdateProfileDirectories(true, true);
#endif

#if 0
	// did the user change the icons path?
	if (is_changed(Preferences::global_path_type::ICONS))
	{
		m_icon_loader.RefreshIcons();
		if (m_machine_view->GetItemCount() > 0)
			m_machine_view->RefreshItems(0, m_machine_view->GetItemCount() - 1);
	}
#endif
}


//-------------------------------------------------
//  on_actionAbout_triggered
//-------------------------------------------------

void MainWindow::on_actionAbout_triggered()
{
	AboutDialog dlg;
	dlg.exec();
}


//-------------------------------------------------
//  on_actionRefreshMachineInfo_triggered
//-------------------------------------------------

void MainWindow::on_actionRefreshMachineInfo_triggered()
{
	refreshMameInfoDatabase();
}


//-------------------------------------------------
//  on_actionBletchMameWebSite_triggered
//-------------------------------------------------

void MainWindow::on_actionBletchMameWebSite_triggered()
{
	QDesktopServices::openUrl(QUrl("https://www.bletchmame.org/"));
}


//-------------------------------------------------
//  on_machinesTableView_activated
//-------------------------------------------------

void MainWindow::on_machinesTableView_activated(const QModelIndex &index)
{
	const info::machine machine = GetMachineFromIndex(index.row());
	Run(machine);
}


//-------------------------------------------------
//  on_tabWidget_currentChanged
//-------------------------------------------------

void MainWindow::on_tabWidget_currentChanged(int index)
{
	Preferences::list_view_type list_view_type = static_cast<Preferences::list_view_type>(index);
	m_prefs.SetSelectedTab(list_view_type);

	switch (list_view_type)
	{
	case Preferences::list_view_type::SOFTWARELIST:
		m_software_list_collection_machine_name.clear();
		updateSoftwareList();
		break;
	}
}


//-------------------------------------------------
//  event
//-------------------------------------------------

bool MainWindow::event(QEvent *event)
{
	bool result;
	if (event->type() == VersionResultEvent::eventId())
	{
		result = onVersionCompleted(static_cast<VersionResultEvent &>(*event));
	}
	else if (event->type() == ListXmlResultEvent::eventId())
	{
		result = onListXmlCompleted(static_cast<ListXmlResultEvent &>(*event));
	}
	else if (event->type() == RunMachineCompletedEvent::eventId())
	{
		result = onRunMachineCompleted(static_cast<RunMachineCompletedEvent &>(*event));
	}
	else if (event->type() == StatusUpdateEvent::eventId())
	{
		result = onStatusUpdate(static_cast<StatusUpdateEvent &>(*event));
	}
	else if (event->type() == ChatterEvent::eventId())
	{
		result = onChatter(static_cast<ChatterEvent &>(*event));
	}
	else
	{
		result = QMainWindow::event(event);
	}
	return result;
}


//-------------------------------------------------
//  IsMameExecutablePresent
//-------------------------------------------------

bool MainWindow::IsMameExecutablePresent() const
{
	const QString &path = m_prefs.GetGlobalPath(Preferences::global_path_type::EMU_EXECUTABLE);
	return !path.isEmpty() && wxFileExists(path);
}


//-------------------------------------------------
//  InitialCheckMameInfoDatabase - called when we
//	load up for the very first time
//-------------------------------------------------

void MainWindow::InitialCheckMameInfoDatabase()
{
	bool done = false;
	while (!done)
	{
		switch (CheckMameInfoDatabase())
		{
		case check_mame_info_status::SUCCESS:
			// we're good!
			done = true;
			break;

		case check_mame_info_status::MAME_NOT_FOUND:
			// prompt the user for the MAME executable
			if (!PromptForMameExecutable())
			{
				// the (l)user gave up; guess we're done...
				done = true;
			}
			break;

		case check_mame_info_status::DB_NEEDS_REBUILD:
			// start a rebuild; whether the process succeeds or fails, we're done
			refreshMameInfoDatabase();
			done = true;
			break;

		default:
			throw false;
		}
	}
}


//-------------------------------------------------
//  CheckMameInfoDatabase - checks the version and
//	the MAME info DB
//
//	how to respond to failure conditions is up to
//	the caller
//-------------------------------------------------

MainWindow::check_mame_info_status MainWindow::CheckMameInfoDatabase()
{
	// first thing, check to see if the executable is there
	if (!IsMameExecutablePresent())
		return check_mame_info_status::MAME_NOT_FOUND;

	// get the version - this should be blazingly fast
	m_client.launch(create_version_task());
	while (m_client.IsTaskActive())
	{
		QCoreApplication::processEvents();
		QThread::yieldCurrentThread();
	}

	// we didn't get a version?  treat this as if we cannot find the
	// executable
	if (m_mame_version.isEmpty())
		return check_mame_info_status::MAME_NOT_FOUND;

	// now let's try to open the info DB; we expect a specific version
	QString db_path = m_prefs.GetMameXmlDatabasePath();
	if (!m_info_db.load(db_path, m_mame_version))
		return check_mame_info_status::DB_NEEDS_REBUILD;

	// success!  we can update the machine list
	return check_mame_info_status::SUCCESS;
}


//-------------------------------------------------
//  PromptForMameExecutable
//-------------------------------------------------

bool MainWindow::PromptForMameExecutable()
{
	QString path = PathsDialog::browseForPathDialog(*this, Preferences::global_path_type::EMU_EXECUTABLE, m_prefs.GetGlobalPath(Preferences::global_path_type::EMU_EXECUTABLE));
	if (path.isEmpty())
		return false;

	m_prefs.SetGlobalPath(Preferences::global_path_type::EMU_EXECUTABLE, std::move(path));
	return true;
}


//-------------------------------------------------
//  refreshMameInfoDatabase
//-------------------------------------------------

bool MainWindow::refreshMameInfoDatabase()
{
	// sanity check; bail if we can't find the executable
	if (!IsMameExecutablePresent())
		return false;

	// list XML
	QString db_path = m_prefs.GetMameXmlDatabasePath();
	m_client.launch(create_list_xml_task(std::move(db_path)));

	// and show the dialog
	{
		LoadingDialog dlg(*this, [this]() { return !m_client.IsTaskActive(); });
		dlg.exec();
		if (dlg.result() != QDialog::DialogCode::Accepted)
		{
			m_client.abort();
			return false;
		}
	}

	// we've succeeded; load the DB
	if (!m_info_db.load(db_path))
	{
		// a failure here is likely due to a very strange condition (e.g. - someone deleting the infodb
		// file out from under me)
		return false;
	}

	return true;
}


//-------------------------------------------------
//  AttachToRootPanel
//-------------------------------------------------

bool MainWindow::AttachToRootPanel() const
{
	// Targetting subwindows with -attach_window was introduced in between MAME 0.217 and MAME 0.218
	const MameVersion REQUIRED_MAME_VERSION_ATTACH_TO_CHILD_WINDOW = MameVersion(0, 217, true);

	// Are we the required version?
	return isMameVersionAtLeast(REQUIRED_MAME_VERSION_ATTACH_TO_CHILD_WINDOW);
}


//-------------------------------------------------
//  Run
//-------------------------------------------------

void MainWindow::Run(const info::machine &machine, const software_list::software *software, void *profile)
{
	// run a "preflight check" on MAME, to catch obvious problems that might not be caught or reported well
	QString preflight_errors = preflightCheck();
	if (!preflight_errors.isEmpty())
	{
		messageBox(preflight_errors);
		return;
	}

	// identify the software name; we either used what was passed in, or we use what is in a profile
	// for which no images are mounted (suggesting a fresh launch)
	QString software_name;
	if (software)
		software_name = software->m_name;
#if HAVE_PROFILES
	else if (profile && profile->images().empty())
		software_name = profile->software();
#endif

	// we need to have full information to support the emulation session; retrieve
	// fake a pauser to forestall "PAUSED" from appearing in the menu bar
	Pauser fake_pauser(*this, false);

	// run the emulation
	Task::ptr task = std::make_shared<RunMachineTask>(
		machine,
		std::move(software_name),
		AttachToRootPanel() ? *m_ui->rootWidget : *this);
	m_client.launch(std::move(task));

	// set up running state and subscribe to events
	m_state.emplace();

	// execute the start handler for all aspects
	for (const auto &aspect : m_aspects)
		aspect->start();

	// we have a session running; hide/show things respectively
	updateMenuBar();

	// set the focus to the main window
	setFocus();

	// wait for first ping
	m_pinging = true;
	while (m_pinging)
	{
		if (!m_state.has_value())
			return;
		QCoreApplication::processEvents();
		QThread::yieldCurrentThread();
	}

	// set up profile (if we have one)
#if HAVE_PROFILES
	m_current_profile_path = profile ? profile->path() : util::g_empty_string;
	m_current_profile_auto_save_state = profile ? profile->auto_save_states() : false;
	if (profile)
	{
		// load all images
		for (const auto &image : profile->images())
			Issue({ "load", image.m_tag, image.m_path });

		// if we have a save state, start it
		if (profile->auto_save_states())
		{
			QString save_state_path = profiles::profile::change_path_save_state(profile->path());
			if (wxFile::Exists(save_state_path))
				Issue({ "state_load", save_state_path });
		}
	}
#endif

	// do we have any images that require images?
	auto iter = std::find_if(m_state->images().get().cbegin(), m_state->images().get().cend(), [](const status::image &image)
	{
		return image.m_must_be_loaded && image.m_file_name.isEmpty();
	});
	if (iter != m_state->images().get().cend())
	{
		throw std::logic_error("NYI");
#if 0
		// if so, show the dialog
		ImagesHost images_host(*this);
		if (!show_images_dialog_cancellable(images_host))
		{
			Issue("exit");
			return;
		}
#endif
	}

	// unpause
	ChangePaused(false);
}


//-------------------------------------------------
//  preflightCheck - run checks on MAME to catch
//	obvious problems when they are easier to
//	diagnose (MAME's error reporting is hard for
//	BletchMAME to decipher)
//-------------------------------------------------

QString MainWindow::preflightCheck() const
{
	// get a list of the plugin paths, checking for the obvious problem where there are no paths
	std::vector<QString> paths = m_prefs.GetSplitPaths(Preferences::global_path_type::PLUGINS);
	if (paths.empty())
		return QString("No plug-in paths are specified.  Under these circumstances, the required \"%1\" plug-in cannot be loaded.").arg(WORKER_UI_PLUGIN_NAME);

	// apply substitutions and normalize the paths
	for (QString &path : paths)
	{
		// apply variable substituions
		path = m_prefs.ApplySubstitutions(path);

		// normalize path separators
		path = QDir::fromNativeSeparators(path);

		// if there is no trailing '/', append one
		if (!path.endsWith('/'))
			path += '/';
	}

	// local function to check for plug in files
	auto checkForPluginFiles = [&paths](const std::initializer_list<QString> &files)
	{
		bool success = util::find_if_ptr(paths, [&paths, &files](const QString &path)
		{
			for (const QString &file : files)
			{		
				QFileInfo fi(path + file);
				if (fi.exists() && fi.isFile())
					return true;
			}
			return false;
		});
		return success;
	};

	// local function to get all paths as a string (for error reporting)
	auto getAllPaths = [&paths]()
	{
		QString result;
		for (const QString &path : paths)
		{
			result += QDir::toNativeSeparators(path);
			result += "\n";
		}
		return result;
	};

	// check to see if worker_ui exists
	if (!checkForPluginFiles({ QString(WORKER_UI_PLUGIN_NAME "/init.lua"), QString(WORKER_UI_PLUGIN_NAME "/plugin.json") }))
	{
		auto message = QString("Could not find the %1 plug-in in the following directories:\n\n%2");
		return message.arg(WORKER_UI_PLUGIN_NAME, getAllPaths());
	}

	// check to see if boot.lua exists
	if (!checkForPluginFiles({ QString("boot.lua") }))
	{
		auto message = QString("Could not find boot.lua in the following directories:\n\n%1");
		return message.arg(getAllPaths());
	}

	// success!
	return QString();
}


//-------------------------------------------------
//  messageBox
//-------------------------------------------------

QMessageBox::StandardButton MainWindow::messageBox(const QString &message, QMessageBox::StandardButtons buttons)
{
	Pauser pauser(*this);

	QMessageBox msgBox(this);
	msgBox.setText(message);
	msgBox.setWindowTitle("BletchMAME");
	msgBox.setStandardButtons(buttons);
	return (QMessageBox::StandardButton) msgBox.exec();
}


//-------------------------------------------------
//  closeEvent
//-------------------------------------------------

void MainWindow::closeEvent(QCloseEvent *event)
{
	if (m_state.has_value())
	{
		// prompt the user, if appropriate
		if (shouldPromptOnStop())
		{
			QString message = "Do you really want to exit?\n"
				"\n"
				"All data in emulated RAM will be lost";
			if (messageBox(message, QMessageBox::Yes | QMessageBox::No) != QMessageBox::StandardButton::Yes)
			{
				event->ignore();
				return;
			}
		}

		// issue exit command so we can shut down the emulation session gracefully
		InvokeExit();
		while (m_state.has_value())
		{
			QCoreApplication::processEvents();
			QThread::yieldCurrentThread();
		}
	}

	// yup, we're closing
	event->accept();
}


//-------------------------------------------------
//  shouldPromptOnStop
//-------------------------------------------------

bool MainWindow::shouldPromptOnStop() const
{
#if HAVE_PROFILE
	return m_current_profile_path.empty() || !m_current_profile_auto_save_state;
#else
	return true;
#endif
}


//-------------------------------------------------
//  isMameVersionAtLeast
//-------------------------------------------------

bool MainWindow::isMameVersionAtLeast(const MameVersion &version) const
{
	return MameVersion(m_mame_version).IsAtLeast(version);
}


//-------------------------------------------------
//  onVersionCompleted
//-------------------------------------------------

bool MainWindow::onVersionCompleted(VersionResultEvent &event)
{
	m_mame_version = std::move(event.m_version);

	// warn the user if this is version of MAME is not supported
	if (!isMameVersionAtLeast(REQUIRED_MAME_VERSION))
	{
		QString message = QString("This version of MAME doesn't seem to be supported; BletchMAME requires MAME %1.%2 or newer to function correctly").arg(
			QString::number(REQUIRED_MAME_VERSION.Major()),
			QString::number(REQUIRED_MAME_VERSION.Minor()));
		messageBox(message);
	}

	m_client.waitForCompletion();
	return true;
}


//-------------------------------------------------
//  onListXmlCompleted
//-------------------------------------------------

bool MainWindow::onListXmlCompleted(const ListXmlResultEvent &event)
{
	// check the status
	switch (event.status())
	{
	case ListXmlResultEvent::Status::SUCCESS:
		// if it succeeded, try to load the DB
		{
			QString db_path = m_prefs.GetMameXmlDatabasePath();
			m_info_db.load(db_path);
		}
		break;

	case ListXmlResultEvent::Status::ABORTED:
		// if we aborted, do nothing
		break;

	case ListXmlResultEvent::Status::ERROR:
		// present an error message
		messageBox(!event.errorMessage().isEmpty()
			? event.errorMessage()
			: "Error building MAME info database");
		break;

	default:
		throw false;
	}

	m_client.waitForCompletion();
	return true;
}


//-------------------------------------------------
//  setupSearchBox
//-------------------------------------------------

void MainWindow::setupSearchBox(QLineEdit &lineEdit, const char *collection_view_desc_name, CollectionViewModel &collectionViewModel)
{
	const QString &text = m_prefs.GetSearchBoxText(collection_view_desc_name);
	lineEdit.setText(text);

	auto callback = [&collectionViewModel, &lineEdit, collection_view_desc_name, this]()
	{
		QString text = lineEdit.text();
		m_prefs.SetSearchBoxText(collection_view_desc_name, std::move(text));
		collectionViewModel.updateListView();
	};
	connect(&lineEdit, &QLineEdit::textEdited, this, callback);
}


//-------------------------------------------------
//  onRunMachineCompleted
//-------------------------------------------------

bool MainWindow::onRunMachineCompleted(const RunMachineCompletedEvent &event)
{
	// update the profile, if present
#if HAVE_PROFILES
	if (!m_current_profile_path.empty())
	{
		std::optional<profiles::profile> profile = profiles::profile::load(m_current_profile_path);
		if (profile)
		{
			profile->images().clear();
			for (const status::image &status_image : m_state->images().get())
			{
				if (!status_image.m_file_name.empty())
				{
					profiles::image &profile_image = profile->images().emplace_back();
					profile_image.m_tag = status_image.m_tag;
					profile_image.m_path = status_image.m_file_name;
				}
			}
			profile->save();
		}
	}
#endif

	// clear out all of the state
	m_client.waitForCompletion();
	m_state.reset();
#if HAVE_PROFILES
	m_current_profile_path = util::g_empty_string;
	m_current_profile_auto_save_state = false;
#endif

	// execute the stop handler for all aspects
	for (const auto &aspect : m_aspects)
		aspect->stop();

	// report any errors
	if (!event.errorMessage().isEmpty())
	{
		messageBox(event.errorMessage());
	}
	return true;
}


//-------------------------------------------------
//  updateSoftwareList
//-------------------------------------------------

void MainWindow::updateSoftwareList()
{
	long selected = m_machinesViewModel->getFirstSelected();
	if (selected >= 0)
	{
		int actual_selected = m_machinesViewModel->getActualIndex(selected);
		info::machine machine = m_info_db.machines()[actual_selected];
		if (machine.name() != m_software_list_collection_machine_name)
		{
			m_software_list_collection.load(m_prefs, machine);
			m_software_list_collection_machine_name = machine.name();
		}
		m_softwareListViewModel->Load(m_software_list_collection, false);
	}
	else
	{
		m_softwareListViewModel->Clear();
	}
	m_softwareListViewModel->updateListView();
}


//-------------------------------------------------
//  onStatusUpdate
//-------------------------------------------------

bool MainWindow::onStatusUpdate(StatusUpdateEvent &event)
{
	m_state->update(event.detachStatus());
	m_pinging = false;
	return true;
}


//-------------------------------------------------
//  onChatter
//-------------------------------------------------

bool MainWindow::onChatter(const ChatterEvent &event)
{
	return true;
}


//-------------------------------------------------
//  GetMachineFromIndex
//-------------------------------------------------

info::machine MainWindow::GetMachineFromIndex(long item) const
{
	// look up the indirection
	int machine_index = m_machinesViewModel->getActualIndex(item);

	// and look up in the info DB
	return m_info_db.machines()[machine_index];
}


//-------------------------------------------------
//  GetMachineListItemText
//-------------------------------------------------

const QString &MainWindow::GetMachineListItemText(info::machine machine, long column) const
{
	switch (column)
	{
	case 0:	return machine.name();
	case 1:	return machine.description();
	case 2:	return machine.year();
	case 3:	return machine.manufacturer();
	}
	throw false;
}


//-------------------------------------------------
//  observeTitleBarText
//-------------------------------------------------

observable::value<QString> MainWindow::observeTitleBarText()
{
	// identify the correct text for paused and not paused
	const QString &machineDesc = m_client.GetCurrentTask<RunMachineTask>()->getMachine().description();
	QString titleTextNotPaused = QString("%1: %2").arg(
		QCoreApplication::applicationName(),
		machineDesc);
	QString titleTextPaused = QString("%1: %2 PAUSED").arg(
		QCoreApplication::applicationName(),
		machineDesc);

	// and observe the result
	return observable::observe(observable::select(
		m_state->paused(),
		titleTextPaused,
		titleTextNotPaused));
}


//-------------------------------------------------
//  updateMenuBar
//-------------------------------------------------

void MainWindow::updateMenuBar()
{
	// are we supposed to show the menu bar?
	m_menu_bar_shown = !m_state.has_value() || m_prefs.GetMenuBarShown();

	// is this different than the current state?
	if (m_menu_bar_shown.get() != m_ui->menubar->isVisible())
	{
		// when we hide the menu bar, we disable the accelerators
		// TODO?

		// show/hide the menu bar
		m_ui->menubar->setVisible(m_menu_bar_shown.get());
	}
}


//**************************************************************************
//  RUNTIME CONTROL
//
//	Actions that affect MAME at runtime go here.  The naming convention is
//	that "invocation actions" take the form InvokeXyz(), whereas methods
//	that change something take the form ChangeXyz()
//**************************************************************************

//-------------------------------------------------
//  Issue
//-------------------------------------------------

void MainWindow::Issue(const std::vector<QString> &args)
{
	std::shared_ptr<RunMachineTask> task = m_client.GetCurrentTask<RunMachineTask>();
	if (!task)
		return;

	task->issue(args);
}


void MainWindow::Issue(const std::initializer_list<std::string> &args)
{
	std::vector<QString> qargs;
	qargs.reserve(args.size());

	for (const auto &arg : args)
	{
		QString qarg = QString::fromStdString(arg);
		qargs.push_back(std::move(qarg));
	}

	Issue(qargs);
}


void MainWindow::Issue(const char *command)
{
	QString command_string = command;
	Issue({ command_string });
}


//-------------------------------------------------
//  InvokePing
//-------------------------------------------------

void MainWindow::InvokePing()
{
	// only issue a ping if there is an active session, and there is no ping in flight
	if (!m_pinging && m_state.has_value())
	{
		m_pinging = true;
		Issue("ping");
	}
}


//-------------------------------------------------
//  InvokeExit
//-------------------------------------------------

void MainWindow::InvokeExit()
{
#if HAVE_PROFILES
	if (m_current_profile_auto_save_state)
	{
		QString save_state_path = profiles::profile::change_path_save_state(m_current_profile_path);
		Issue({ "state_save_and_exit", save_state_path });
	}
	else
#endif
	{
		Issue({ "exit" });
	}
}


//-------------------------------------------------
//  ChangePaused
//-------------------------------------------------

void MainWindow::ChangePaused(bool paused)
{
	Issue(paused ? "pause" : "resume");
}


//-------------------------------------------------
//  ChangeThrottled
//-------------------------------------------------

void MainWindow::ChangeThrottled(bool throttled)
{
	Issue({ "throttled", std::to_string(throttled ? 1 : 0) });
}


//-------------------------------------------------
//  ChangeThrottleRate
//-------------------------------------------------

void MainWindow::ChangeThrottleRate(float throttle_rate)
{
	Issue({ "throttle_rate", std::to_string(throttle_rate) });
}


//-------------------------------------------------
//  ChangeThrottleRate
//-------------------------------------------------

void MainWindow::ChangeThrottleRate(int adjustment)
{
	// find where we are in the array
	int index;
	for (index = 0; index < sizeof(s_throttle_rates) / sizeof(s_throttle_rates[0]); index++)
	{
		if (m_state->throttle_rate().get() >= s_throttle_rates[index])
			break;
	}

	// apply the adjustment
	index += adjustment;
	index = std::max(index, 0);
	index = std::min(index, (int)(sizeof(s_throttle_rates) / sizeof(s_throttle_rates[0]) - 1));

	// and change the throttle rate
	ChangeThrottleRate(s_throttle_rates[index]);
}


//-------------------------------------------------
//  ChangeSound
//-------------------------------------------------

void MainWindow::ChangeSound(bool sound_enabled)
{
	Issue({ "set_attenuation", std::to_string(sound_enabled ? SOUND_ATTENUATION_ON : SOUND_ATTENUATION_OFF) });
}
