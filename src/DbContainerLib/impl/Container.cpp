#include "stdafx.h"
#include "Container.h"
#include "ContainerAPI.h"
#include "DataStorageBinaryFile.h"
#include "ContainerResourcesImpl.h"
#include "ContainerException.h"
#include "Crypto.h"
#include "ContainerInfoImpl.h"
#include "CommonUtils.h"
#include "FsUtils.h"

const int dbc::Container::ROOT_ID = 1;

using namespace dbc;

namespace
{
	void ClearDB(Connection& connection)
	{
        std::string tables[] = { "Sets", "FileSystem", "FileStreams" };
		dbc::SQLQuery query = connection.CreateQuery();
		std::string dropCommand("DROP TABLE ");
        for (const std::string& table : tables)
		{
            query.Prepare(dropCommand + table + ";");
			query.Step();
		}

		query.Prepare("VACUUM");
		query.Step();
	}

	void WriteTables(Connection& connection)
	{
		SQLQuery query = connection.CreateQuery();
        std::list<std::string> queries;
        queries.push_back("CREATE TABLE Sets(id INTEGER PRIMARY KEY NOT NULL, storage_data_size INTEGER, storage_data BLOB);");
        queries.push_back("CREATE TABLE FileSystem(id INTEGER PRIMARY KEY NOT NULL, parent_id INTEGER, name TEXT, type INTEGER, created INTEGER, modified INTEGER, meta TEXT);");
        queries.push_back("CREATE TABLE FileStreams(id INTEGER PRIMARY KEY NOT NULL, file_id INTEGER NOT NULL, stream_order INTEGER, start INTEGER, size INTEGER, used INTEGER);");

        for (const std::string& tableCreationQuery : queries)
		{
			try
			{
                query.Prepare(tableCreationQuery);
				query.Step();
			}
			catch (const dbc::ContainerException& ex)
			{
                throw ContainerException(ERR_DB, CANT_CREATE, ex.ErrorCode());
			}
		}
	}

	void WriteRoot(Connection& connection)
	{
		// Creating root folder
        SQLQuery query = connection.CreateQuery("INSERT INTO FileSystem(parent_id, name, type, created, modified) VALUES (?, ?, ?, ?, ?);");

		query.BindInt(1, 0);
        std::string rootName(1, dbc::PATH_SEPARATOR);
        query.BindText(2, rootName);
		query.BindInt(3, static_cast<int>(dbc::ElementTypeFolder));
		ElementProperties elProps;
        elProps.SetCurrentTime();
        query.BindInt64(4, elProps.DateCreated());
        query.BindInt64(5, elProps.DateModified());
		query.Step();
	}

	void WriteSets(Connection& connection)
	{
		//SQLQuery query(db, L"INSERT INTO Sets(path_sep) VALUES (?);");
		// TODO: Write another sets
	}

	void BuildDB(Connection& connection)
	{
		WriteTables(connection);
		WriteRoot(connection);
		WriteSets(connection);
	}

	/*Error DBIsEmpty(sqlite3 * db)
	{
		Error ret(ERR_DB, IS_EMPTY); // Expected = default
		const int queries_count = 3;
		const std::string tables[queries_count] = { "Sets", "FileSystem", "FileStreams" };

		try
		{
			SQLQuery query(db, "SELECT count(name) FROM sqlite_master where name=?;");
			for (int i = 0; i < queries_count; ++i)
			{
				query.Reset();
				query.BindText(tables[i], 1);
				query.Step();
				if (query.ColumnInt(1) != 0) // Not expected
				{
					ret = Error(ERR_DB, ALREADY_EXISTS);
					break;
				}
			}
		}
		catch (const dbc::ContainerException &ex)
		{
			ret = ex.ErrType();
		}
		return ret;
	}*/

	bool CheckDBValidy(dbc::Connection &db)
	{
		return true;
		// TODO: implement proper DB validation
	}

	void SetDBPragma(dbc::Connection &db)
	{
		dbc::SQLQuery query(db, "PRAGMA auto_vacuum = FULL;");
		query.Step();
	}
}

dbc::Container::Container(const std::string& path, const std::string& password, bool create)
	: m_dbFile(path), m_connection(path, create), m_storage(new dbc::DataStorageBinaryFile)
{
	PrepareContainer(password, create);
}

dbc::Container::Container(const std::string& path, const std::string& password, IDataStorageGuard storage, bool create)
	: m_dbFile(path), m_connection(path, create), m_storage(storage)
{
	PrepareContainer(password, create);
}

dbc::Container::~Container()
{
	ContaierResourcesImpl* resources = dynamic_cast<ContaierResourcesImpl*>(m_resources.get());
	assert(resources != nullptr && "Wrong container resources implementation class is used");

	resources->ReportContainerDied();
}

void dbc::Container::Clear()
{
	try
	{
		ClearDB(m_connection);
		m_storage->ClearData();
	}
	catch (const ContainerException &ex)
	{
		throw ContainerException(ERR_DB, CANT_REMOVE, ex.ErrorCode());
	}

	try
	{
		BuildDB(m_connection);
	}
	catch (const ContainerException &ex)
	{
		throw ContainerException(ERR_DB, CANT_CREATE, ex.ErrorCode());
	}
}

void dbc::Container::ResetPassword(const std::string& newPassword)
{
	m_storage->ResetPassword(newPassword);
}

std::string dbc::Container::GetPath() const
{
	return m_dbFile;
}

dbc::FolderGuard dbc::Container::GetRoot()
{
	return FolderGuard(new Folder(m_resources, ROOT_ID));
}

dbc::ElementGuard dbc::Container::GetElement(const std::string& path)
{
	if (path.empty())
	{
		throw ContainerException(WRONG_PARAMETERS);
	}

	std::vector<std::string> names;
	dbc::utils::SplitSavingDelim(path, PATH_SEPARATOR, names);

	int64_t parentId = 0;
	int elementType = ElementTypeUnknown;

	SQLQuery query(m_connection, "SELECT id, type FROM FileSystem WHERE parent_id = ? AND name = ?;");
	for (std::vector<std::string>::iterator itr = names.begin(); itr != names.end(); ++itr, query.Reset())
	{
		query.BindInt64(1, parentId);
		*itr = dbc::utils::UnslashedPath(*itr);
		query.BindText(2, *itr);
		if (!query.Step())
		{
			return ElementGuard(nullptr);
		}
		parentId = query.ColumnInt64(0);
		elementType = query.ColumnInt(1);
		if (query.Step()) // If there is one more file with the same name in the same directory
		{
			throw ContainerException(ERR_DB, IS_DAMAGED);
		}
	}
	return CreateElementObject(parentId, static_cast<ElementType>(elementType));
}

ContainerInfo dbc::Container::GetInfo()
{
	return ContainerInfo(new ContainerInfoImpl(m_resources));
}

dbc::DataUsagePreferences dbc::Container::GetDataUsagePreferences() const
{
	return m_dataUsagePrefs;
}

void dbc::Container::SetDataUsagePreferences(const DataUsagePreferences& prefs)
{
	m_dataUsagePrefs = prefs;
}

dbc::ElementGuard dbc::Container::GetElement(int64_t id)
{
	SQLQuery query(m_connection, "SELECT type FROM FileSystem WHERE id = ?;");
	query.BindInt64(1, id);
	if (!query.Step())
	{
		throw ContainerException(Element::s_notFoundError);
	}
	int type = query.ColumnInt(0);
	return CreateElementObject(id, static_cast<ElementType>(type));
}

dbc::ElementGuard dbc::Container::CreateElementObject(int64_t id, ElementType type)
{
	switch (type)
	{
	case ElementTypeFolder:
		return ElementGuard(new Folder(m_resources, id));
	case ElementTypeFile:
		return ElementGuard(new File(m_resources, id));
	case ElementTypeSymLink:
		return ElementGuard(new SymLink(m_resources, id));
	case ElementTypeDirectLink:
		return ElementGuard(new DirectLink(m_resources, id));
	default:
		assert(!"Unknown element type specified");
		throw ContainerException(ERR_INTERNAL);
	}
}

dbc::ElementGuard dbc::Container::CreateElementObject(int64_t parentId, const std::string& name, ElementType type)
{
	switch (type)
	{
	case ElementTypeFolder:
		return ElementGuard(new Folder(m_resources, parentId, name));
	case ElementTypeFile:
		return ElementGuard(new File(m_resources, parentId, name));
	case ElementTypeSymLink:
		return ElementGuard(new SymLink(m_resources, parentId, name));
	case ElementTypeDirectLink:
		return ElementGuard(new DirectLink(m_resources, parentId, name));
	default:
		assert(!"Unknown element type specified");
		throw ContainerException(ERR_INTERNAL);
	}
}

void dbc::Container::PrepareContainer(const std::string& password, bool create)
{
	assert(m_storage.get() != nullptr);
	if (create) // create DB and storage
	{
		BuildDB(m_connection);
		m_storage->Create(m_dbFile, password);
	}
	else 
	{
		if (!CheckDBValidy(m_connection)) // check existing DB
		{
			throw ContainerException(ERR_DB, CANT_OPEN, ERR_DB, NOT_VALID);
		}
		// check Data
		RawData storageData;
		ReadSets(storageData);
		m_storage->Open(m_dbFile, password, storageData);
		// TODO: Parse storage data
	}
	SetDBPragma(m_connection);

	m_resources.reset(new ContaierResourcesImpl(*this, m_connection, *m_storage));
}

void dbc::Container::ReadSets(RawData& storageData)
{
	assert(storageData.empty());
	SQLQuery query(m_connection, "SELECT storage_data FROM Sets WHERE id = 1;");
	query.Step();
	query.ColumnBlob(1, storageData);
}

void dbc::Container::SaveStorageData()
{
	assert(m_storage.get() != nullptr);
	RawData storageData;
	m_storage->GetDataToSave(storageData);
	if (storageData.size() > 0)
	{
		SQLQuery query(m_connection, "UPDATE Sets SET storage_data = ? WHERE id = 1;");
		query.BindBlob(1, storageData);
	}
}
