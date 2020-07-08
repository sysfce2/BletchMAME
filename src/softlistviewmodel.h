/***************************************************************************

	softlistviewmodel.h

	Software list view

***************************************************************************/

#pragma once

#ifndef SOFTLISTVIEW_H
#define SOFTLISTVIEW_H

#include "softwarelist.h"
#include "collectionviewmodel.h"
#include "info.h"

#define SOFTLIST_VIEW_DESC_NAME "softlist"


// ======================> SoftwareListViewModel
class SoftwareListViewModel : public CollectionViewModel
{
public:
	SoftwareListViewModel(QTableView &tableView, Preferences &prefs);

	void Load(const software_list_collection &software_col, bool load_parts, const QString &dev_interface = util::g_empty_string);
	void Clear();
	QString GetSelectedItem() const;
	const software_list::software *GetSelectedSoftware() const;

protected:
	virtual const QString &getListViewSelection() const override;
	virtual void setListViewSelection(const QString &selection) override;

private:
	// ======================> SoftwareAndPart
	class SoftwareAndPart
	{
	public:
		SoftwareAndPart(const software_list &sl, const software_list::software &sw, const software_list::part *p)
			: m_softlist(sl)
			, m_software(sw)
			, m_part(p)
		{
		}

		const software_list &softlist() const { return m_softlist; }
		const software_list::software &software() const { return m_software; }
		const software_list::part &part() const { assert(m_part); return *m_part; }
		bool has_part() const { return m_part != nullptr; }

	private:
		const software_list &m_softlist;
		const software_list::software &m_software;
		const software_list::part *m_part;
	};

	std::vector<SoftwareAndPart>	m_parts;
	std::vector<QString>			m_softlist_names;

	const QString &GetListItemText(const software_list::software &sw, long column);
	const SoftwareAndPart *GetSelectedSoftwareAndPart() const;
};


#endif // SOFTLISTVIEW_H
