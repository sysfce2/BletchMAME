/***************************************************************************

    dialogs/switches.cpp

    Configuration & DIP Switches customization dialog

***************************************************************************/

#include <QLabel>
#include <QComboBox>
#include <QStringListModel>

#include "dialogs/switches.h"
#include "ui_inputs.h"


//**************************************************************************
//  IMPLEMENTATION
//**************************************************************************

//-------------------------------------------------
//  ctor
//-------------------------------------------------

SwitchesDialog::SwitchesDialog(QWidget &parent, const QString &title, ISwitchesHost &host, status::input::input_class input_class, info::machine machine)
    : m_host(host)
    , m_input_class(input_class)
	, m_machine(machine)
{
	// set up UI
	m_ui = std::make_unique<Ui::InputsDialog>();
	m_ui->setupUi(this);

	// caller decides the window title
	setWindowTitle(title);

	// update the inputs
	UpdateInputs();
}


//-------------------------------------------------
//  dtor
//-------------------------------------------------

SwitchesDialog::~SwitchesDialog()
{
}


//-------------------------------------------------
//  UpdateInputs
//-------------------------------------------------

void SwitchesDialog::UpdateInputs()
{
	const int COLUMN_LABEL = 0;
	const int COLUMN_COMBO_BOX = 1;
	int row = 0;

	for (const status::input &input : m_host.GetInputs())
	{
		if (input.m_class == m_input_class)
		{
			// get all choices
			std::unordered_map<std::uint32_t, QString> choices = GetChoices(input);

			// get the values and strings
			std::vector<std::uint32_t> choice_values;
			QStringList choice_strings;
			for (const auto &[value, str] : choices)
			{
				choice_values.push_back(value);
				choice_strings.push_back(str);
			}

			// add static text with the name of the config item
			QLabel &label = *new QLabel(input.m_name, this);
			m_ui->gridLayout->addWidget(&label, row, COLUMN_LABEL);

			// create a combo box with the values
			QStringListModel &comboBoxModel = *new QStringListModel(choice_strings, this);
			QComboBox &comboBox = *new QComboBox(this);
			comboBox.setModel(&comboBoxModel);
			m_ui->gridLayout->addWidget(&comboBox, row, COLUMN_COMBO_BOX);

			// select the proper value
			auto iter = std::find(choice_values.begin(), choice_values.end(), input.m_value);
			if (iter != choice_values.end())
				comboBox.setCurrentIndex(iter - choice_values.begin());
				
			// bind the event
			QString port_tag = input.m_port_tag;
			ioport_value mask = input.m_mask;
			connect(&comboBox, static_cast<void (QComboBox:: *)(int)>(&QComboBox::currentIndexChanged), this, [this, port_tag{std::move(port_tag)}, mask{std::move(mask)}, choice_values{std::move(choice_values)}](int index)
			{
				// get the value out of choices
				ioport_value value = choice_values[index];

				// and specify it
				m_host.SetInputValue(port_tag, mask, value);
			});

			// next row
			row++;
		}
	}
}


//-------------------------------------------------
//  GetChoices
//-------------------------------------------------

std::unordered_map<std::uint32_t, QString> SwitchesDialog::GetChoices(const status::input &input) const
{
	// get the tag
	const QString &tag = input.m_port_tag;

	// find the configuration with this tag
	std::unordered_map<std::uint32_t, QString> results;
	auto iter = std::find_if(
		m_machine.configurations().begin(),
		m_machine.configurations().end(),
		[&input, &tag](const info::configuration c)
		{
			return c.tag() == tag && c.mask() == input.m_mask;
		});

	// did we found it?
	if (iter != m_machine.configurations().end())
	{
		// we did - map its setting values
		info::configuration configuration = *iter;
		for (const info::configuration_setting setting : configuration.settings())
			results[setting.value()] = setting.name();
	}

	return results;
}
