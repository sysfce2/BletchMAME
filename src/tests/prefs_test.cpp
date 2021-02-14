/***************************************************************************

    prefs_test.cpp

    Unit tests for prefs.cpp

***************************************************************************/

#include <QBuffer>
#include <QTextStream>

#include "prefs.h"
#include "test.h"

class Preferences::Test : public QObject
{
    Q_OBJECT

private slots:
	void general();
	void pathNames();
	void globalGetPathCategory();
	void machineGetPathCategory();
	void substitutions1();
	void substitutions2();
	void substitutions3();

private:
	static QString fixPaths(QString &&s);
	void substitutions(const char *input, const char *expected);
};


//**************************************************************************
//  IMPLEMENTATION
//**************************************************************************

//-------------------------------------------------
//  general
//-------------------------------------------------

void Preferences::Test::general()
{
	// read the prefs.xml test case into a QString and fix it
	QString text;
	{
		QFile file(":/resources/prefs.xml");
		QVERIFY(file.open(QIODevice::ReadOnly));
		QTextStream textStream(&file);
		text = fixPaths(textStream.readAll());
	}

	// and put the text back into a buffer
	QBuffer buffer;
	QVERIFY(buffer.open(QIODevice::ReadWrite));
	{
		QTextStream textStream(&buffer);
		textStream << text;
	}
	QVERIFY(buffer.seek(0));

	// read the prefs
	Preferences prefs;
	prefs.Load(buffer);

	// validate the results
	QVERIFY(prefs.GetGlobalPath(Preferences::global_path_type::EMU_EXECUTABLE)					== fixPaths("C:\\mame64.exe"));
	QVERIFY(prefs.GetGlobalPath(Preferences::global_path_type::ROMS)							== fixPaths("C:\\roms\\"));
	QVERIFY(prefs.GetGlobalPath(Preferences::global_path_type::SAMPLES)							== fixPaths("C:\\samples\\"));
	QVERIFY(prefs.GetGlobalPath(Preferences::global_path_type::CONFIG)							== fixPaths("C:\\cfg\\"));
	QVERIFY(prefs.GetGlobalPath(Preferences::global_path_type::NVRAM)							== fixPaths("C:\\nvram\\"));
	QVERIFY(prefs.GetMachinePath("echo", Preferences::machine_path_type::WORKING_DIRECTORY)		== fixPaths("C:\\MyEchoGames\\"));
	QVERIFY(prefs.GetMachinePath("echo", Preferences::machine_path_type::LAST_SAVE_STATE)		== fixPaths("C:\\MyLastState.sta"));
	QVERIFY(prefs.GetMachinePath("foxtrot", Preferences::machine_path_type::WORKING_DIRECTORY)	== fixPaths(""));
	QVERIFY(prefs.GetMachinePath("foxtrot", Preferences::machine_path_type::LAST_SAVE_STATE)	== fixPaths(""));
}


//-------------------------------------------------
//  fixPaths - make it easier to write a test case
//	that doesn't involve a lot of path nonsense
//-------------------------------------------------

QString Preferences::Test::fixPaths(QString &&s)
{
#ifdef Qd_OS_WIN32
	bool applyFix = false;
#else
	bool applyFix = true;
#endif

	if (applyFix)
	{
		// this is hacky, but it works in practice
		s = s.replace("C:\\", "/");
		s = s.replace("\\", "/");
		s = s.replace(".exe", "");
	}
	return s;
}


//-------------------------------------------------
//  pathNames
//-------------------------------------------------

void Preferences::Test::pathNames()
{
	auto iter = std::find(s_path_names.begin(), s_path_names.end(), nullptr);
	QVERIFY(iter == s_path_names.end());
}


//-------------------------------------------------
//  globalGetPathCategory
//-------------------------------------------------

void Preferences::Test::globalGetPathCategory()
{
	for (Preferences::global_path_type type : util::all_enums<Preferences::global_path_type>())
		Preferences::GetPathCategory(type);
}


//-------------------------------------------------
//  machineGetPathCategory
//-------------------------------------------------

void Preferences::Test::machineGetPathCategory()
{
	for (Preferences::machine_path_type type : util::all_enums<Preferences::machine_path_type>())
		Preferences::GetPathCategory(type);
}


//-------------------------------------------------
//  substitutions
//-------------------------------------------------

void Preferences::Test::substitutions(const char *input, const char *expected)
{
	auto func = [](const QString &var_name)
	{
		return var_name == "VARNAME"
			? "vardata"
			: QString();
	};

	QString actual = Preferences::InternalApplySubstitutions(QString(input), func);
	assert(actual == expected);
	(void)actual;
	(void)expected;
}


void Preferences::Test::substitutions1() { substitutions("C:\\foo", "C:\\foo"); }
void Preferences::Test::substitutions2() { substitutions("C:\\foo (with parens)", "C:\\foo (with parens)"); }
void Preferences::Test::substitutions3() { substitutions("C:\\$(VARNAME)\\foo", "C:\\vardata\\foo"); }


static TestFixture<Preferences::Test> fixture;
#include "prefs_test.moc"
