#pragma once

#include "collection_model.hpp"

namespace core
{
	// 用户组织关系与来源 Collection 分开；成员只引用队列顶层项目。
	struct queue_group
	{
		std::string id, name;
		bool collapsed {false};
		std::vector<std::string> items;
	};
	struct queue_group_snapshot
	{
		queue_group group;
		collection_summary summary;
	};
	std::string queue_group_name(std::string name);
	void retain_queue_groups(std::vector<queue_group> &groups, const std::vector<std::string> &items);
}
