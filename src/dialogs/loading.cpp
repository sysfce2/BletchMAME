/***************************************************************************

	dialogs/loading.cpp

	"loading MAME Info..." modal dialog

***************************************************************************/

#include <memory>
#include <wx/app.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>

#include "dialogs/loading.h"
#include "wxhelpers.h"


//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

namespace
{
	class LoadingDialog : public wxDialog
	{
	public:
		LoadingDialog(wxWindow &parent, const std::function<bool()> &poll_completion_check);

	private:
		const std::function<bool()> &	m_poll_completion_check;
		wxTimer							m_timer;

		template<typename TControl, typename... TArgs> TControl &AddControl(std::unique_ptr<wxBoxSizer> &sizer, int proportion, int flags, TArgs&&... args);
	};
};


//**************************************************************************
//  IMPLEMENTATION
//**************************************************************************

//-------------------------------------------------
//  ctor
//-------------------------------------------------

LoadingDialog::LoadingDialog(wxWindow &parent, const std::function<bool()> &poll_completion_check)
	: wxDialog(&parent, wxID_ANY, wxTheApp->GetAppName())
	, m_poll_completion_check(poll_completion_check)
	, m_timer(this, wxID_ANY)
{
	// the message
	wxStaticText *static_text = new wxStaticText(this, wxID_ANY, "Building MAME info database...");

	// and specify the layout
	SpecifySizerAndFit(*this, { boxsizer_orientation::VERTICAL, 10, {
		{ 0, wxALL,					*static_text },
		{ 0, wxALL | wxALIGN_RIGHT,	CreateButtonSizer(wxCANCEL) }
	}});
	Centre();

	// bind events
	Bind(wxEVT_TIMER, [this](auto &) { if (m_poll_completion_check()) EndDialog(wxID_OK); });

	// start the timer
	m_timer.Start(100);
}


//-------------------------------------------------
//  AddControl
//-------------------------------------------------

template<typename TControl, typename... TArgs>
TControl &LoadingDialog::AddControl(std::unique_ptr<wxBoxSizer> &sizer, int proportion, int flags, TArgs&&... args)
{
	TControl *control = new TControl(this, std::forward<TArgs>(args)...);
	sizer->Add(control, proportion, flags, 4);
	return *control;
}


//-------------------------------------------------
//  show_loading_mame_info_dialog
//-------------------------------------------------

bool show_loading_mame_info_dialog(wxWindow &parent, const std::function<bool()> &poll_completed)
{
	LoadingDialog dialog(parent, poll_completed);
	return dialog.ShowModal() == wxID_OK;
}
