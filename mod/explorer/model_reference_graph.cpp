// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "explorer_model.h"
#include "model_shared.h"

#include "reference_graph_layout.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace URK::Unity;

namespace Explorer {

	void RuntimeModel::clear_reference_graph() {
		for (auto& [_, handle] : reference_graph_handles_)
			Inspect::FreeObjectHandle(handle);
		reference_graph_handles_.clear();
		working_.reference_graph = {};
	}

	void RuntimeModel::build_reference_graph(const Command& command) {
		clear_reference_graph();
		Object root{};
		if (command.object_inspector_token != 0 && working_.object_inspector.valid &&
			working_.object_inspector.token == command.object_inspector_token)
			root = Inspect::ResolveObjectHandle(object_inspector_handle_);
		else if (working_.selected_instance_id != 0)
			root = resolve_selected_object();
		if (!root) {
			set_status("Reference graph needs a selected GameObject or open Object Inspector target");
			return;
		}

		struct Pending {
			Inspect::ObjectHandle handle;
			std::size_t node = 0;
			std::size_t depth = 0;
		};
		constexpr std::size_t kMaxNodes = 128;
		constexpr std::size_t kMaxEdges = 384;
		constexpr std::size_t kMaxArrayElements = 64;
		constexpr std::size_t kMaxDepth = 4;
		std::deque<Pending> pending;
		std::unordered_map<void*, std::size_t> seen;
		std::vector<ReferenceGraphLayout::NodePosition> positions;
		const URK::managed::Class* unity_object_base =
			URK::managed::find_class("", "UnityEngine", "Object");
		const URK::managed::Class* component_base =
			URK::managed::find_class("", "UnityEngine", "Component");

		const auto add_node = [&](Object object, std::size_t depth) -> std::optional<std::size_t> {
			if (!object.handle())
				return std::nullopt;
			if (const auto existing = seen.find(object.handle()); existing != seen.end())
				return existing->second;
			if (working_.reference_graph.nodes.size() >= kMaxNodes) {
				working_.reference_graph.truncated = true;
				return std::nullopt;
			}
			Inspect::ObjectHandle traversal = Inspect::PinObject(object);
			Inspect::ObjectHandle retained = Inspect::WeakObject(object);
			if (!traversal.handle || !retained.handle) {
				Inspect::FreeObjectHandle(traversal);
				Inspect::FreeObjectHandle(retained);
				return std::nullopt;
			}
			const Inspect::TypeInfo type = safe_type_of(object);
			const std::uint64_t token = 0xd000000000000000ull |
				(next_reference_token_++ & 0x0fffffffffffffffull);
			reference_graph_handles_[token] = retained;
			Snapshot::ReferenceGraph::Node node{};
			node.token = token;
			node.type_name = type.full_name.empty() ? "<unknown managed type>" : type.full_name;
			node.label = node.type_name;
			const auto* runtime_class = static_cast<const URK::managed::Class*>(type.handle);
			if (runtime_class && URK::managed::class_get_element_class(runtime_class))
				node.kind = Snapshot::ReferenceGraph::Node::Kind::Array;
			else if (type.full_name == "UnityEngine.GameObject")
				node.kind = Snapshot::ReferenceGraph::Node::Kind::GameObject;
			else if (component_base && runtime_class &&
				URK::managed::class_is_assignable_from(component_base, runtime_class) != 0)
				node.kind = Snapshot::ReferenceGraph::Node::Kind::Component;
			else if (unity_object_base && runtime_class &&
				URK::managed::class_is_assignable_from(unity_object_base, runtime_class) != 0)
				node.kind = Snapshot::ReferenceGraph::Node::Kind::UnityObject;
			if (unity_object_base && type.handle &&
				URK::managed::class_is_assignable_from(unity_object_base,
					static_cast<const URK::managed::Class*>(type.handle)) != 0) {
				const std::string name = object.name();
				if (!name.empty())
					node.label = name;
			}
			node.pointer_text = pointer_text(object.handle());
			node.depth = depth;
			const std::size_t index = working_.reference_graph.nodes.size();
			working_.reference_graph.nodes.push_back(std::move(node));
			positions.push_back({depth});
			seen[object.handle()] = index;
			pending.push_back({traversal, index, depth});
			return index;
		};
		const auto add_edge = [&](std::size_t from, std::size_t to, std::string label,
			Snapshot::ReferenceGraph::Edge::Kind kind) {
			if (from >= working_.reference_graph.nodes.size() || to >= working_.reference_graph.nodes.size())
				return;
			if ((kind == Snapshot::ReferenceGraph::Edge::Kind::Field ||
				kind == Snapshot::ReferenceGraph::Edge::Kind::ArrayElement) &&
				working_.reference_graph.nodes[to].depth <= working_.reference_graph.nodes[from].depth)
				kind = Snapshot::ReferenceGraph::Edge::Kind::BackReference;
			working_.reference_graph.edges.push_back({from, to, std::move(label), kind});
		};

		const std::optional<std::size_t> root_node = add_node(root, 0);
		const Inspect::TypeInfo root_type = safe_type_of(root);
		if (root_node && root_type.full_name == "UnityEngine.GameObject") {
			const auto components = GameObject{root.handle()}.GetComponentsRooted<Object>();
			for (const Object& component : components) {
				if (const auto child = add_node(component, 1))
					add_edge(*root_node, *child, "component", Snapshot::ReferenceGraph::Edge::Kind::Component);
			}
		}
		while (!pending.empty() && working_.reference_graph.edges.size() < kMaxEdges) {
			Pending current = std::move(pending.front());
			pending.pop_front();
			const Object object = Inspect::ResolveObjectHandle(current.handle);
			if (!object || current.depth >= kMaxDepth) {
				Inspect::FreeObjectHandle(current.handle);
				continue;
			}
			const URK::managed::Class* klass =
				URK::managed::object_get_class(static_cast<URK::managed::Object*>(object.handle()));
			if (!klass) {
				Inspect::FreeObjectHandle(current.handle);
				continue;
			}
			const bool is_array = URK::managed::class_get_element_class(klass) != nullptr;
			if (component_base && URK::managed::class_is_assignable_from(component_base, klass) != 0) {
				const GameObject owner = Component{object.handle()}.gameObject();
				if (owner) {
					if (const auto child = add_node(owner, current.depth + 1))
						add_edge(current.node, *child, "gameObject", Snapshot::ReferenceGraph::Edge::Kind::Owner);
				}
			}
			if (is_array) {
				Inspect::ValueInfo array{};
				array.kind = Inspect::ValueKind::ArrayReference;
				array.object = object.handle();
				const std::size_t length = std::min(Inspect::ArrayLength(array), kMaxArrayElements);
				for (std::size_t index = 0; index < length && working_.reference_graph.edges.size() < kMaxEdges; ++index) {
					const Inspect::ValueInfo value = Inspect::ReadArrayElement(array, index);
					if ((value.kind != Inspect::ValueKind::ObjectReference &&
						value.kind != Inspect::ValueKind::ArrayReference) || !value.object)
						continue;
					if (const auto child = add_node(Object{value.object}, current.depth + 1))
						add_edge(current.node, *child, "[" + std::to_string(index) + "]",
							Snapshot::ReferenceGraph::Edge::Kind::ArrayElement);
				}
			}
			else {
				for (const Inspect::FieldInfo& field : Inspect::fields_from_class(klass, true)) {
					if (field.is_static || field.is_value_type || working_.reference_graph.edges.size() >= kMaxEdges)
						continue;
					const Inspect::ValueInfo value = Inspect::ReadField(object, field);
					if ((value.kind != Inspect::ValueKind::ObjectReference &&
						value.kind != Inspect::ValueKind::ArrayReference) || !value.object)
						continue;
					if (const auto child = add_node(Object{value.object}, current.depth + 1))
						add_edge(current.node, *child, field.name, Snapshot::ReferenceGraph::Edge::Kind::Field);
				}
			}
			Inspect::FreeObjectHandle(current.handle);
		}
		for (Pending& remaining : pending)
			Inspect::FreeObjectHandle(remaining.handle);
		if (working_.reference_graph.edges.size() >= kMaxEdges)
			working_.reference_graph.truncated = true;
		std::vector<ReferenceGraphLayout::Edge> layout_edges;
		layout_edges.reserve(working_.reference_graph.edges.size());
		for (const Snapshot::ReferenceGraph::Edge& edge : working_.reference_graph.edges)
			layout_edges.push_back({edge.from, edge.to});
		ReferenceGraphLayout::arrange(positions, layout_edges);
		for (std::size_t index = 0; index < positions.size(); ++index) {
			working_.reference_graph.nodes[index].x = positions[index].x;
			working_.reference_graph.nodes[index].y = positions[index].y;
		}
		working_.reference_graph.status = std::to_string(working_.reference_graph.nodes.size()) + " nodes, " +
			std::to_string(working_.reference_graph.edges.size()) + " references";
		set_status("Built reference graph: " + working_.reference_graph.status);
	}

} // namespace Explorer
