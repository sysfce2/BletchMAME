/***************************************************************************

    dialogs/paths.h

    Paths dialog

***************************************************************************/

#pragma once

#ifndef DIALOGS_PATHS_H
#define DIALOGS_PATHS_H

#include <QDialog>
#include <QWidget>
#include <QString>
#include <memory>
#include <vector>

#include "prefs.h"

QT_BEGIN_NAMESPACE
namespace Ui { class PathsDialog; }
QT_END_NAMESPACE

class PathsListViewModel;


// ======================> PathsDialog

class PathsDialog : public QDialog
{
	Q_OBJECT
public:
	class Test;

	// ctor/dtor
	PathsDialog(QWidget &parent, Preferences &prefs);
	~PathsDialog();

	// methods
	void persist();

	// static methods
	static QString browseForPathDialog(QWidget &parent, Preferences::global_path_type type, const QString &default_path);

private slots:
	void on_comboBox_currentIndexChanged(int index);
	void on_browseButton_clicked();
	void on_insertButton_clicked();
	void on_deleteButton_clicked();
	void on_listView_activated(const QModelIndex &index);

private:
	static const size_t PATH_COUNT = util::enum_count<Preferences::global_path_type>();
	static const QStringList s_combo_box_strings;

	class Model
	{
	public:
		// ctor
		Model(Preferences &prefs, PathsListViewModel &listViewModel);

		// methods
		void setCurrentPathType(Preferences::global_path_type newPathType);
		void persist();
		const Preferences &prefs() const;

		// statics
		static QString joinPaths(const QStringList &pathList);
		static QStringList splitPaths(const QString &paths);

	private:
		Preferences &									m_prefs;
		PathsListViewModel &							m_listViewModel;
		std::optional<Preferences::global_path_type>	m_currentPathType;
		std::array<QString, PATH_COUNT>					m_pathLists;

		// private methods
		void extractPathsFromListView();
	};

	std::unique_ptr<Ui::PathsDialog>					m_ui;
	std::optional<Model>								m_model;

	// methods
	void updateButtonsEnabled();
	Preferences::global_path_type getCurrentPath() const;
	bool browseForPath(int item);
	PathsListViewModel &listModel();
	const PathsListViewModel &listModel() const;
	bool canonicalizeAndValidate(QString &path);

	// static methods
	static QStringList buildComboBoxStrings();
};


#endif // DIALOGS_PATHS_H
