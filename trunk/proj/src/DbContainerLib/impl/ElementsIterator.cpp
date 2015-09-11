#include "stdafx.h"
#include "ElementsIterator.h"
#include "ContainerFolder.h"
#include "ContainerFile.h"
#include "SQLQuery.h"
#include "ContainerException.h"

using namespace dbc;

namespace
{
	void GetChildrenInfo(Connection& connection, int64_t folder_id, ElementInfo_vt& out)
	{
		assert(out.empty());
		int64_t tmp_id;
		int64_t tmp_parent_id;
		int tmp_type;
		SQLQuery query(connection, "SELECT id, parent_id, type FROM FileSystem WHERE parent_id = ?;");
		query.BindInt64(1, folder_id);
		while (query.Step())
		{
			tmp_id = query.ColumnInt64(0);
			tmp_parent_id = query.ColumnInt64(1);
			tmp_type = query.ColumnInt(2);
			if (tmp_type != ElementTypeFolder && tmp_type != ElementTypeFile)
			{
				throw dbc::ContainerException(ERR_DB, IS_DAMAGED);
			}
			ElementInfo tmp_info(tmp_id, tmp_parent_id, static_cast<ElementType>(tmp_type));
			out.push_back(tmp_info);
		}
	}
}

ElementsIterator::ElementsIterator(ContainerResources resources, int64_t folder_id)
: m_resources(resources), m_folder_id(folder_id)
{
	GetChildrenInfo(m_resources->GetConnection(), folder_id, m_info);
	m_size = m_info.size();
}

ContainerElementGuard ElementsIterator::Next()
{
	if (!HasNext())
	{
		throw ContainerException(WRONG_PARAMETERS);
	}

		ElementInfo current = m_info[m_current++];
		switch (current.Type)
		{
		case ElementTypeFolder:
			return ContainerElementGuard(new ContainerFolder(m_resources, current.ID));
		case ElementTypeFile:
			return ContainerElementGuard(new ContainerFile(m_resources, current.ID));
		default:
			throw ContainerException(ERR_INTERNAL);
		}
}