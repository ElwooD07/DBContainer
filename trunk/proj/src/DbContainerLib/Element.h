#pragma once
#include "ElementProperties.h"
#include "ContainerResources.h"
#include "Types.h"
#include "ErrorCodes.h"
#include <memory>

namespace dbc
{
	class SQLQuery;

	class Folder;
	class File;
	class SymLink;
	class DirectLink;

	typedef std::shared_ptr<Folder> FolderGuard;
	typedef std::shared_ptr<File> FileGuard;

	class Element
	{
	public:
		Element(ContainerResources resources, int64_t id);
		Element(ContainerResources resources, int64_t parentId, const std::string& name);

		virtual bool Exists();
		virtual std::string Name();
		virtual std::string Path();
		virtual ElementType Type() const;

		virtual Folder* AsFolder();
		virtual File* AsFile();
		virtual SymLink* AsSymLink();
		virtual DirectLink* AsDirectLink();

		virtual bool IsTheSame(const Element& obj) const;
		virtual bool IsChildOf(const Element& obj);

		virtual FolderGuard GetParentEntry();

		virtual void MoveToEntry(Folder& newParent);
		virtual void Remove();
		virtual void Rename(const std::string& newName);

		virtual void GetProperties(ElementProperties& out);
		virtual void ResetProperties(const std::string& tag);

		static Error notFoundError;

	protected:
		void Refresh();
		Error Exists(int64_t parent_id, std::string name); // Returns s_errElementNotFound (see .cpp) as false and SUCCESS as true, or other error code if there was an error
		void WriteProps();
		void UpdateSpecificData(const RawData& specificData);
		int64_t GetId(const Element& element);

	protected:
		ContainerResources m_resources;

		int64_t m_id;
		int64_t m_parentId;
		ElementType m_type;
		std::string m_name;
		ElementProperties m_props;
		RawData m_specificData;

	private:
		void InitElementInfo(SQLQuery& query, int typeN, int propsN, int specificDataN);
	};

	typedef std::shared_ptr<Element> ElementGuard;
}