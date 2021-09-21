/***************************************************************************

    dialogs/paths.cpp

    Paths dialog

***************************************************************************/

#include <QPushButton>
#include <QFileDialog>
#include <QTextStream>
#include <QStringListModel>

#include "dialogs/paths.h"
#include "ui_paths.h"
#include "assetfinder.h"
#include "pathslistviewmodel.h"
#include "prefs.h"
#include "utility.h"


//**************************************************************************
//  IMPLEMENTATION
//**************************************************************************

const QStringList PathsDialog::s_combo_box_strings = buildComboBoxStrings();

#ifdef Q_OS_WIN32
#define PATH_LIST_SEPARATOR	";"
#else
#define PATH_LIST_SEPARATOR	":"
#endif


//-------------------------------------------------
//  ctor
//-------------------------------------------------

PathsDialog::PathsDialog(QWidget &parent, Preferences &prefs)
	: QDialog(&parent)
	, m_prefs(prefs)
	, m_listViewModelCurrentPath({ })
{
	m_ui = std::make_unique<Ui::PathsDialog>();
	m_ui->setupUi(this);

	// path data
	for (size_t i = 0; i < PATH_COUNT; i++)
		m_pathLists[i] = m_prefs.getGlobalPath(static_cast<Preferences::global_path_type>(i));

	// list view
	PathsListViewModel &model = *new PathsListViewModel([this](QString &path) { return canonicalizeAndValidate(path); }, this);
	m_ui->listView->setModel(&model);

	// combo box
	QStringListModel &comboBoxModel = *new QStringListModel(s_combo_box_strings, this);
	m_ui->comboBox->setModel(&comboBoxModel);

	// listen to selection changes
	connect(&model, &QAbstractItemModel::modelReset, this, [this]()
	{
		updateButtonsEnabled();
	});
	connect(&model, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &, const QModelIndex &)
	{
		updateButtonsEnabled();
	});
	connect(m_ui->listView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](const QItemSelection &, const QItemSelection &)
	{
		updateButtonsEnabled();
	});
	updateButtonsEnabled();
}


//-------------------------------------------------
//  dtor
//-------------------------------------------------

PathsDialog::~PathsDialog()
{
}


//-------------------------------------------------
//  persist - persist all of our state to prefs
//-------------------------------------------------

std::vector<Preferences::global_path_type> PathsDialog::persist()
{
	// first we want to make sure that m_pathLists is up to data
	extractPathsFromListView();

	// we want to return a vector identifying the paths that got changed
	std::vector<Preferences::global_path_type> changedPaths;
	for (Preferences::global_path_type type : util::all_enums<Preferences::global_path_type>())
	{
		QString &path = m_pathLists[static_cast<size_t>(type)];

		// has this path changed?
		if (path != m_prefs.getGlobalPath(type))
		{
			// if so, record that it changed
			m_prefs.setGlobalPath(type, std::move(path));
			changedPaths.push_back(type);
		}
	}
	return changedPaths;
}


//-------------------------------------------------
//  extractPathsFromListView
//-------------------------------------------------

void PathsDialog::extractPathsFromListView()
{
	if (m_listViewModelCurrentPath.has_value())
	{
		// reflect changes on the m_current_path_list back into m_pathLists 
		QStringList paths = listModel().paths();
		QString pathString = joinPaths(paths);
		m_pathLists[(int)m_listViewModelCurrentPath.value()] = std::move(pathString);
	}
}


//-------------------------------------------------
//  updateCurrentPathList
//-------------------------------------------------

void PathsDialog::updateCurrentPathList()
{
	const Preferences::global_path_type currentPathType = getCurrentPath();
	Preferences::PathCategory category = Preferences::getPathCategory(currentPathType);

	QStringList paths = splitPaths(m_pathLists[(int)currentPathType]);

	bool isMulti = category == Preferences::PathCategory::MultipleDirectories
		|| category == Preferences::PathCategory::MultipleDirectoriesOrArchives;

	listModel().setPaths(std::move(paths), isMulti);

	m_listViewModelCurrentPath = currentPathType;
}


//-------------------------------------------------
//  buildComboBoxStrings
//-------------------------------------------------

QStringList PathsDialog::buildComboBoxStrings()
{
	std::array<QString, PATH_COUNT> paths;
	paths[(size_t)Preferences::global_path_type::EMU_EXECUTABLE] = "MAME Executable";
	paths[(size_t)Preferences::global_path_type::ROMS] = "ROMs";
	paths[(size_t)Preferences::global_path_type::SAMPLES] = "Samples";
	paths[(size_t)Preferences::global_path_type::CONFIG] = "Config Files";
	paths[(size_t)Preferences::global_path_type::NVRAM] = "NVRAM Files";
	paths[(size_t)Preferences::global_path_type::HASH] = "Hash Files";
	paths[(size_t)Preferences::global_path_type::ARTWORK] = "Artwork Files";
	paths[(size_t)Preferences::global_path_type::ICONS] = "Icons";
	paths[(size_t)Preferences::global_path_type::PLUGINS] = "Plugins";
	paths[(size_t)Preferences::global_path_type::PROFILES] = "Profiles";
	paths[(size_t)Preferences::global_path_type::CHEATS] = "Cheats";
	paths[(size_t)Preferences::global_path_type::SNAPSHOTS] = "Snapshots";

	QStringList result;
	for (QString &str : paths)
		result.push_back(std::move(str));
	return result;
}


//-------------------------------------------------
//  getCurrentPath
//-------------------------------------------------

Preferences::global_path_type PathsDialog::getCurrentPath() const
{
	int selection = m_ui->comboBox->currentIndex();
	return static_cast<Preferences::global_path_type>(selection);
}


//-------------------------------------------------
//  browseForPath
//-------------------------------------------------

bool PathsDialog::browseForPath(int index)
{
	// show the file dialog
	QString path = browseForPathDialog(
		*this,
		getCurrentPath(),
		index < listModel().pathCount() ? listModel().path(index) : "");
	if (path.isEmpty())
		return false;

	// specify it
	listModel().setPath(index, std::move(path));
	return true;
}


//-------------------------------------------------
//  browseForPathDialog
//-------------------------------------------------

QString PathsDialog::browseForPathDialog(QWidget &parent, Preferences::global_path_type type, const QString &default_path)
{
	QString caption, filter;
	switch (type)
	{
	case Preferences::global_path_type::EMU_EXECUTABLE:
		caption = "Specify MAME Path";
#ifdef Q_OS_WIN32
		filter = "EXE files (*.exe);*.exe";
#endif
		break;

	default:
		caption = "Specify Path";
		break;
	};

	// determine the FileMode
	QFileDialog::FileMode fileMode;
	switch (Preferences::getPathCategory(type))
	{
	case Preferences::PathCategory::SingleFile:
		fileMode = QFileDialog::FileMode::ExistingFile;
		break;

	case Preferences::PathCategory::SingleDirectory:
	case Preferences::PathCategory::MultipleDirectories:
	case Preferences::PathCategory::MultipleDirectoriesOrArchives:
		fileMode = QFileDialog::FileMode::Directory;
		break;

	default:
		throw false;
	}

	// show the dialog
	QFileDialog dialog(&parent, caption, default_path, filter);
	dialog.setFileMode(fileMode);
	dialog.setAcceptMode(QFileDialog::AcceptMode::AcceptOpen);
	dialog.exec();
	return dialog.result() == QDialog::DialogCode::Accepted
		? dialog.selectedFiles().first()
		: QString();
}


//-------------------------------------------------
//  canonicalizeAndValidate
//-------------------------------------------------

bool PathsDialog::canonicalizeAndValidate(QString &path)
{
	// first get off of native separators
	path = QDir::fromNativeSeparators(path);

	// do we need to apply substitutions?
	const Preferences::global_path_type currentPathType = getCurrentPath();
	bool applySubstitutions = currentPathType != Preferences::global_path_type::EMU_EXECUTABLE;

	// if so, apply them
	QString pathAfterSubstitutions = applySubstitutions
		? m_prefs.applySubstitutions(path)
		: path;

	// check the file
	bool isValid = false;
	QFileInfo fi(pathAfterSubstitutions);
	if (fi.exists())
	{
		Preferences::PathCategory category = Preferences::getPathCategory(currentPathType);
		switch (category)
		{
		case Preferences::PathCategory::SingleFile:
			isValid = fi.isFile();
			break;

		case Preferences::PathCategory::SingleDirectory:
		case Preferences::PathCategory::MultipleDirectories:
			isValid = fi.isDir();
			break;

		case Preferences::PathCategory::MultipleDirectoriesOrArchives:
			isValid = fi.isDir()
				|| (fi.isFile() && AssetFinder::isValidArchive(pathAfterSubstitutions));
			break;

		default:
			throw false;
		}

		// ensure a trailing slash if we're a directory
		if (fi.isDir() && !path.endsWith('/'))
			path += '/';
	}

	// and convert to native separators
	path = QDir::toNativeSeparators(path);

	return isValid;
}


//-------------------------------------------------
//  splitPaths
//-------------------------------------------------

QStringList PathsDialog::splitPaths(const QString &paths)
{
	return paths.isEmpty()
		? QStringList()
		: paths.split(PATH_LIST_SEPARATOR);
}


//-------------------------------------------------
//  listModel
//-------------------------------------------------

QString PathsDialog::joinPaths(const QStringList &pathList)
{
	return pathList.join(PATH_LIST_SEPARATOR);
}


//-------------------------------------------------
//  listModel
//-------------------------------------------------

PathsListViewModel &PathsDialog::listModel()
{
	return *dynamic_cast<PathsListViewModel *>(m_ui->listView->model());
}


//-------------------------------------------------
//  listModel
//-------------------------------------------------

const PathsListViewModel &PathsDialog::listModel() const
{
	return *dynamic_cast<const PathsListViewModel *>(m_ui->listView->model());
}


//-------------------------------------------------
//  updateButtonsEnabled
//-------------------------------------------------

void PathsDialog::updateButtonsEnabled()
{
	// get the selection
	QModelIndexList selectedIndexes = m_ui->listView->selectionModel()->selectedIndexes();

	// interrogate the model
	std::optional<int> browseTarget = listModel().getBrowseTargetForSelection(selectedIndexes);
	std::optional<int> insertTarget = listModel().getInsertTargetForSelection(selectedIndexes);
	std::vector<int> deleteTargets = listModel().getDeleteTargetsForSelection(selectedIndexes);

	// set the buttons accordingly
	m_ui->browseButton->setEnabled(browseTarget.has_value());
	m_ui->insertButton->setEnabled(insertTarget.has_value());
	m_ui->deleteButton->setEnabled(deleteTargets.size() > 0);
}


//-------------------------------------------------
//  on_comboBox_currentIndexChanged
//-------------------------------------------------

void PathsDialog::on_comboBox_currentIndexChanged(int index)
{
	updateCurrentPathList();
}


//-------------------------------------------------
//  on_browseButton_clicked
//-------------------------------------------------

void PathsDialog::on_browseButton_clicked()
{
	// figure out where the browse target entry is
	QModelIndexList selectedIndexes = m_ui->listView->selectionModel()->selectedIndexes();
	std::optional<int> browseTarget = listModel().getBrowseTargetForSelection(selectedIndexes);

	// and do the heavy lifting
	if (browseTarget)
		browseForPath(*browseTarget);
}


//-------------------------------------------------
//  on_insertButton_clicked
//-------------------------------------------------

void PathsDialog::on_insertButton_clicked()
{
	// figure out where the insertion target entry is
	QModelIndexList selectedIndexes = m_ui->listView->selectionModel()->selectedIndexes();
	std::optional<int> insertTarget = listModel().getInsertTargetForSelection(selectedIndexes);

	if (insertTarget)
	{
		// insert a blank item
		listModel().insertPath(*insertTarget, "");

		// and tell the list view to start editing
		QModelIndex index = listModel().index(*insertTarget);
		m_ui->listView->edit(index);
	}
}


//-------------------------------------------------
//  on_deleteButton_clicked
//-------------------------------------------------

void PathsDialog::on_deleteButton_clicked()
{
	// figure out where the deletion target entries are
	QModelIndexList selectedIndexes = m_ui->listView->selectionModel()->selectedIndexes();
	std::vector<int> deleteTargets = listModel().getDeleteTargetsForSelection(selectedIndexes);

	// and delete
	listModel().deletePaths(std::move(deleteTargets));
}


//-------------------------------------------------
//  on_listView_activated
//-------------------------------------------------

void PathsDialog::on_listView_activated(const QModelIndex &index)
{
	browseForPath(index.row());
}
