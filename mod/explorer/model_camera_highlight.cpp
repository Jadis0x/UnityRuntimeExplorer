// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "explorer_model.h"
#include "model_shared.h"

#include "ui/highlight.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace URK::Unity;

namespace Explorer {
	namespace {

		constexpr auto kHighlightCameraRefreshInterval = std::chrono::seconds(1);

	} // namespace

	void RuntimeModel::update_camera_focus() {
		if (!camera_focus_.active())
			return;
		std::string error;
		if (!camera_focus_.update(error)) {
			working_.camera_focus_active = false;
			set_status(error.empty() ? "Camera focus ended" : std::move(error));
		}
	}

	void RuntimeModel::focus_selected_camera(GameObject object) {
		std::string error;
		if (!camera_focus_.start(object, error)) {
			working_.camera_focus_active = false;
			set_status(error.empty() ? "Camera focus failed" : std::move(error));
			return;
		}
		working_.camera_focus_active = true;
		set_status("Camera focused on " + object.name() + " (use Return camera to restore it)");
	}

	void RuntimeModel::restore_focused_camera() {
		if (!camera_focus_.active())
			return;
		std::string error;
		const bool restored = camera_focus_.stop(error);
		working_.camera_focus_active = false;
		set_status(restored ? "Camera restored" :
			"Camera focus ended; " + (error.empty() ? std::string("the original camera was unavailable") : error));
	}
	void RuntimeModel::update_highlight() {
		if (!highlight_enabled_) {
			if (highlight_id_ != 0) {
				ModUI::Highlight::enqueue_remove(highlight_id_);
				highlight_id_ = 0;
			}
			if (highlight_locator_id_ != 0) {
				ModUI::Highlight::enqueue_remove(highlight_locator_id_);
				highlight_locator_id_ = 0;
			}
			return;
		}
		if (!safe_object_alive(selected_))
			return;

		// The shared highlight system is budgeted for many targets at once and
		// re-projects each one every other frame, which on a moving camera makes
		// a single box visibly lag and jitter behind the object it marks. The
		// Explorer highlights one selection, so it pays for a projection every
		// frame and gets a box that sits still.
		static bool update_policy_applied = false;
		if (!update_policy_applied) {
			ModUI::Highlight::UpdatePolicy policy = ModUI::Highlight::update_policy();
			policy.mode = ModUI::Highlight::UpdateMode::EveryFrame;
			policy.projection_interval_frames = 1;
			ModUI::Highlight::set_update_policy(policy);
			update_policy_applied = true;
		}

		ModUI::Highlight::Style style{};
		style.color = IM_COL32(50, 235, 255, 255);
		style.fill_color = IM_COL32(40, 190, 255, 36);
		style.label_color = IM_COL32(255, 255, 255, 255);
		style.label_bg_color = IM_COL32(7, 16, 24, 238);
		style.label_border_color = IM_COL32(80, 220, 255, 255);
		style.shadow_color = IM_COL32(0, 0, 0, 220);
		style.corner_box = true;
		// An outline remains unambiguous without tinting the whole game when a
		// third-party renderer reports pathological bounds.
		style.filled = false;
		style.shadow = true;
		style.thickness = 3.5f;
		style.corner_length = 18.0f;
		style.draw_label = true;
		style.label_above_box = true;
		style.max_distance = highlight_max_distance_;
		// Use renderer bounds when available.
		style.offscreen_indicator = false;
		std::string label = selected_.name();

		const Transform selected_transform = selected_.transform();
		const std::string transform_type = selected_transform ? safe_runtime_class_name(selected_transform) : std::string{};
		const bool is_rect_transform = transform_type == "UnityEngine.RectTransform" ||
			transform_type == "RectTransform";
		// Canvas children require the canvas camera, not necessarily Camera.main.
		const Canvas canvas = selected_transform ? selected_transform.GetComponentInParent<Canvas>(true) : Canvas{};
		const bool is_overlay_canvas = canvas && canvas.renderMode() == CanvasRenderMode::ScreenSpaceOverlay;
		Camera camera = canvas && !is_overlay_canvas ? canvas.worldCamera() : Camera{};

		const Clock::time_point camera_now = Clock::now();
		if (camera_now >= next_highlight_camera_refresh_ || highlight_cameras_.empty()) {
			clear_highlight_camera_cache();
			const auto cameras = Object::FindObjectsOfTypeRooted<Camera>();
			for (const Camera& candidate : cameras) {
				if (safe_object_alive(candidate) && candidate.enabled()) {
					Inspect::ObjectHandle handle = Inspect::WeakObject(Object{ candidate.handle() });
					if (handle.handle)
						highlight_cameras_.push_back(handle);
				}
			}
			next_highlight_camera_refresh_ = camera_now + kHighlightCameraRefreshInterval;
		}

		Camera main_camera{};
		std::vector<Vector3> camera_samples;
		if (!camera && !is_overlay_canvas) {
			main_camera = Camera::main();
			if (!safe_object_alive(main_camera) || !main_camera.enabled())
				main_camera = Camera{};

			camera_samples.reserve(9);
			if (selected_transform)
				camera_samples.push_back(selected_transform.position());
			for (const Inspect::ObjectHandle& renderer_handle : highlight_renderers_) {
				const Renderer renderer{ Inspect::ResolveObjectHandle(renderer_handle).handle() };
				if (!safe_object_alive(renderer))
					continue;
				camera_samples.push_back(renderer.bounds().center);
				if (camera_samples.size() == 9)
					break;
			}

			// Camera.main is the camera that defines the player's screen in the
			// usual case.  Do not replace it merely because a scene/minimap camera
			// can see the object: that made off-screen locators jump to the wrong side.
			camera = main_camera;

			// Untagged games still need a fallback.  Only pick a camera that has a
			// real on-screen sample, so a hidden auxiliary camera cannot win a tie.
			if (!camera) {
				int best_visible_samples = 0;
				int best_front_samples = -1;
				float best_center_distance = std::numeric_limits<float>::max();
				for (const Inspect::ObjectHandle& camera_handle : highlight_cameras_) {
					const Camera candidate{ Inspect::ResolveObjectHandle(camera_handle).handle() };
					if (!safe_object_alive(candidate))
						continue;
					int visible_samples = 0;
					int front_samples = 0;
					float center_distance = 0.0f;
					for (const Vector3& sample : camera_samples) {
						const Vector3 viewport = candidate.WorldToViewportPoint(sample);
						if (!std::isfinite(viewport.x) || !std::isfinite(viewport.y) || !std::isfinite(viewport.z))
							continue;
						if (viewport.z > 0.01f) {
							++front_samples;
							const float dx = viewport.x - 0.5f;
							const float dy = viewport.y - 0.5f;
							center_distance += dx * dx + dy * dy;
							if (viewport.x >= 0.0f && viewport.x <= 1.0f &&
								viewport.y >= 0.0f && viewport.y <= 1.0f)
								++visible_samples;
						}
					}
					if (visible_samples > 0 && (visible_samples > best_visible_samples ||
						(visible_samples == best_visible_samples && front_samples > best_front_samples) ||
						(visible_samples == best_visible_samples && front_samples == best_front_samples &&
							center_distance < best_center_distance))) {
						camera = candidate;
						best_visible_samples = visible_samples;
						best_front_samples = front_samples;
						best_center_distance = center_distance;
					}
				}
			}
		}
		if (!camera)
			camera = main_camera ? main_camera : Camera::main();
		const int unity_screen_width = Screen::width();
		const int unity_screen_height = Screen::height();
		const float screen_width = static_cast<float>(unity_screen_width);
		const float screen_height = static_cast<float>(unity_screen_height);
		if ((!is_overlay_canvas && !camera) || unity_screen_width <= 0 || unity_screen_height <= 0 ||
			screen_width <= 0.0f || screen_height <= 0.0f)
			return;
		// Keep projection in Unity pixels; ImGui state belongs to the render hook.
		const float x_scale = 1.0f;
		const float y_scale = 1.0f;
		const auto to_draw_space = [=](const Vector3& screen) {
			return ImVec2(screen.x * x_scale, screen_height - screen.y * y_scale);
			};
		const auto valid_screen_point = [](const Vector3& point) {
			return point.z > 0.01f && std::isfinite(point.x) && std::isfinite(point.y) &&
				std::isfinite(point.z);
			};

		const Vector3 selected_position = selected_transform ? selected_transform.position() : Vector3{};
		float selected_distance = -1.0f;
		if (camera) {
			const Transform camera_transform = camera.transform();
			if (camera_transform) {
				selected_distance = Vector3::distance(selected_position, camera_transform.position());
				if (std::isfinite(selected_distance) && selected_distance >= 0.0f) {
					char distance_text[48]{};
					std::snprintf(distance_text, sizeof(distance_text), "  |  %.1f m", selected_distance);
					label += distance_text;
				}
				if (highlight_max_distance_ > 0.0f && std::isfinite(selected_distance) &&
					selected_distance > highlight_max_distance_) {
					if (highlight_id_ != 0) {
						ModUI::Highlight::enqueue_remove(highlight_id_);
						highlight_id_ = 0;
					}
					if (highlight_locator_id_ != 0) {
						ModUI::Highlight::enqueue_remove(highlight_locator_id_);
						highlight_locator_id_ = 0;
					}
					return;
				}
			}
		}
		float min_x = 0.0f;
		float min_y = 0.0f;
		float max_x = 0.0f;
		float max_y = 0.0f;
		bool projected = false;
		// Project RectTransform corners rather than its pivot position.
		const auto project_rect_transform = [&] {
			if (!is_rect_transform || !selected_transform)
				return false;
			const Rect rect = RectTransform{ selected_transform.handle() }.rect();
			if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) ||
				!std::isfinite(rect.height) || rect.width <= 0.0f || rect.height <= 0.0f)
				return false;
			const Vector3 local_corners[] = {
				{rect.x, rect.y, 0.0f},
				{rect.x, rect.y + rect.height, 0.0f},
				{rect.x + rect.width, rect.y, 0.0f},
				{rect.x + rect.width, rect.y + rect.height, 0.0f},
			};
			float rect_min_x = std::numeric_limits<float>::max();
			float rect_min_y = std::numeric_limits<float>::max();
			float rect_max_x = std::numeric_limits<float>::lowest();
			float rect_max_y = std::numeric_limits<float>::lowest();
			for (const Vector3& local_corner : local_corners) {
				const Vector3 world_corner = selected_transform.CallExact<Vector3>(
					"TransformPoint", { "UnityEngine.Vector3" }, local_corner);
				ImVec2 draw_corner{};
				if (is_overlay_canvas) {
					if (!std::isfinite(world_corner.x) || !std::isfinite(world_corner.y))
						return false;
					draw_corner = ImVec2(world_corner.x * x_scale, screen_height - world_corner.y * y_scale);
				}
				else {
					if (!camera)
						return false;
					const Vector3 screen_corner = camera.WorldToScreenPoint(world_corner);
					if (!valid_screen_point(screen_corner))
						return false;
					draw_corner = to_draw_space(screen_corner);
				}
				rect_min_x = std::min(rect_min_x, draw_corner.x);
				rect_max_x = std::max(rect_max_x, draw_corner.x);
				rect_min_y = std::min(rect_min_y, draw_corner.y);
				rect_max_y = std::max(rect_max_y, draw_corner.y);
			}
			min_x = rect_min_x;
			min_y = rect_min_y;
			max_x = rect_max_x;
			max_y = rect_max_y;
			return true;
			};
		const bool rect_projected = project_rect_transform();
		projected = rect_projected;
		// Union renderers that plausibly belong to the selected object. Some VFX
		// and UI renderers report a screen-sized bounds even when their GameObject
		// is a small child; accepting those turns a nearby selection into a giant
		// highlight rectangle.
		const float maximum_bounds_radius = selected_distance > 0.01f
			? std::max(2.5f, selected_distance * 1.5f)
			: 100.0f;
		for (const Inspect::ObjectHandle& renderer_handle : highlight_renderers_) {
			const Renderer renderer{ Inspect::ResolveObjectHandle(renderer_handle).handle() };
			if (rect_projected || !camera)
				break;
			if (!safe_object_alive(renderer))
				continue;
			const Bounds bounds = renderer.bounds();
			const float bounds_radius = bounds.extents.magnitude();
			if (!std::isfinite(bounds.center.x) || !std::isfinite(bounds.center.y) ||
				!std::isfinite(bounds.center.z) || !std::isfinite(bounds_radius) ||
				bounds_radius <= 0.0001f || bounds_radius > maximum_bounds_radius)
				continue;
			const Vector3 min = bounds.min();
			const Vector3 max = bounds.max();
			const Vector3 corners[] = {
				{min.x, min.y, min.z}, {min.x, min.y, max.z}, {min.x, max.y, min.z}, {min.x, max.y, max.z},
				{max.x, min.y, min.z}, {max.x, min.y, max.z}, {max.x, max.y, min.z}, {max.x, max.y, max.z},
			};
			float renderer_min_x = std::numeric_limits<float>::max();
			float renderer_min_y = std::numeric_limits<float>::max();
			float renderer_max_x = std::numeric_limits<float>::lowest();
			float renderer_max_y = std::numeric_limits<float>::lowest();
			bool renderer_projected = false;
			for (const Vector3& corner : corners) {
				const Vector3 screen = camera.WorldToScreenPoint(corner);
				if (!valid_screen_point(screen))
					continue;
				const ImVec2 draw_point = to_draw_space(screen);
				// Clamp near-plane samples before computing bounds.
				constexpr float kProjectionOverflow = 1.0f;
				const float x = std::clamp(draw_point.x, -screen_width * kProjectionOverflow,
					screen_width * (1.0f + kProjectionOverflow));
				const float y = std::clamp(draw_point.y, -screen_height * kProjectionOverflow,
					screen_height * (1.0f + kProjectionOverflow));
				renderer_min_x = std::min(renderer_min_x, x);
				renderer_max_x = std::max(renderer_max_x, x);
				renderer_min_y = std::min(renderer_min_y, y);
				renderer_max_y = std::max(renderer_max_y, y);
				renderer_projected = true;
			}
			if (!renderer_projected)
				continue;
			if (!projected) {
				min_x = renderer_min_x;
				min_y = renderer_min_y;
				max_x = renderer_max_x;
				max_y = renderer_max_y;
				projected = true;
			}
			else {
				min_x = std::min(min_x, renderer_min_x);
				min_y = std::min(min_y, renderer_min_y);
				max_x = std::max(max_x, renderer_max_x);
				max_y = std::max(max_y, renderer_max_y);
			}
		}

		constexpr float kFallbackWidth = 34.0f;
		constexpr float kFallbackHeight = 64.0f;
		const auto use_transform_anchor = [&] {
			if (!camera)
				return false;
			const Vector3 anchor = camera.WorldToScreenPoint(selected_position);
			if (!valid_screen_point(anchor))
				return false;
			const ImVec2 draw_anchor = to_draw_space(anchor);
			min_x = draw_anchor.x - kFallbackWidth * 0.5f;
			max_x = draw_anchor.x + kFallbackWidth * 0.5f;
			min_y = draw_anchor.y - kFallbackHeight * 0.5f;
			max_y = draw_anchor.y + kFallbackHeight * 0.5f;
			return true;
			};
		// Use an anchor when the object has no renderer.
		if (!projected && selected_transform) {
			if (is_rect_transform && is_overlay_canvas) {
				min_x = selected_position.x * x_scale - kFallbackWidth * 0.5f;
				max_x = selected_position.x * x_scale + kFallbackWidth * 0.5f;
				min_y = screen_height - selected_position.y * y_scale - kFallbackHeight * 0.5f;
				max_y = screen_height - selected_position.y * y_scale + kFallbackHeight * 0.5f;
				projected = true;
			}
			else {
				projected = use_transform_anchor();
			}
		}

		const auto clear_locator = [&] {
			if (highlight_locator_id_ != 0) {
				ModUI::Highlight::enqueue_remove(highlight_locator_id_);
				highlight_locator_id_ = 0;
			}
			};
		const auto update_offscreen_locator = [&] {
			if (!camera)
				return;
			const Vector3 target_screen = camera.WorldToScreenPoint(selected_position);
			if (!std::isfinite(target_screen.x) || !std::isfinite(target_screen.y) ||
				!std::isfinite(target_screen.z))
				return;
			float locator_x = target_screen.x;
			float locator_y = screen_height - target_screen.y;
			if (target_screen.z <= 0.01f) {
				// Use the camera basis for objects behind the camera.
				const Transform camera_transform = camera.transform();
				if (!camera_transform)
					return;
				const Vector3 offset = selected_position - camera_transform.position();
				const float distance = offset.magnitude();
				if (distance <= 0.0001f)
					return;
				const Vector3 direction = offset / distance;
				float horizontal = Vector3::dot(direction, camera_transform.right());
				float vertical = -Vector3::dot(direction, camera_transform.up());
				const float length = std::sqrt(horizontal * horizontal + vertical * vertical);
				if (length <= 0.0001f) {
					horizontal = 0.0f;
					// A target exactly behind the player has no left/right component.
					// Use the lower edge to communicate "turn around", not the top.
					vertical = 1.0f;
				}
				else {
					horizontal /= length;
					vertical /= length;
				}
				locator_x = screen_width * 0.5f + horizontal * screen_width;
				locator_y = screen_height * 0.5f + vertical * screen_height;
			}
			const float edge_padding = 24.0f;
			const float edge_x = std::clamp(locator_x, edge_padding, screen_width - edge_padding);
			const float edge_y = std::clamp(locator_y, edge_padding, screen_height - edge_padding);
			ModUI::Highlight::Style locator_style = style;
			locator_style.color = IM_COL32(255, 120, 48, 255);
			locator_style.fill_color = IM_COL32(255, 120, 48, 96);
			locator_style.corner_box = false;
			locator_style.filled = false;
			locator_style.thickness = 2.0f;
			locator_style.corner_length = 4.0f;
			locator_style.draw_label = true;
			locator_style.label_above_box = false;
			locator_style.offscreen_indicator = true;
			locator_style.indicator_color = IM_COL32(255, 145, 48, 255);
			locator_style.indicator_thickness = 3.0f;
			locator_style.indicator_head_size = 10.0f;
			locator_style.indicator_center_gap = 0.0f;
			locator_style.indicator_length = 34.0f;
			locator_style.indicator_center_dot_radius = 0.0f;
			const ImVec2 min(edge_x - 3.0f, edge_y - 3.0f);
			const ImVec2 max(edge_x + 3.0f, edge_y + 3.0f);
			const std::string locator_label = label;
			if (highlight_locator_id_ == 0) {
				highlight_locator_id_ = ModUI::Highlight::enqueue_add_screen_rect(
					min, max, locator_label.c_str(), locator_style);
			}
			else {
				ModUI::Highlight::enqueue_set_screen_rect(highlight_locator_id_, min, max);
				ModUI::Highlight::enqueue_set_label(highlight_locator_id_, locator_label.c_str());
			}
			};

			// Use the selected transform; child renderer bounds are unreliable here.
		if (!is_overlay_canvas && camera &&
			!valid_screen_point(camera.WorldToScreenPoint(selected_position))) {
			if (highlight_id_ != 0) {
				ModUI::Highlight::enqueue_remove(highlight_id_);
				highlight_id_ = 0;
			}
			update_offscreen_locator();
			return;
		}

		// Reject screen-dominating bounds and fall back to the off-screen locator.
		if (projected &&
			(max_x - min_x > screen_width * 0.65f || max_y - min_y > screen_height * 0.65f)) {
			projected = use_transform_anchor();
		}
		if (!projected || max_x < 0.0f || max_y < 0.0f || min_x > screen_width || min_y > screen_height) {
			if (highlight_id_ != 0) {
				ModUI::Highlight::enqueue_remove(highlight_id_);
				highlight_id_ = 0;
			}
			update_offscreen_locator();
			return;
		}
		clear_locator();

		constexpr float kPadding = 3.0f;
		constexpr float kMinimumSize = 14.0f;
		min_x -= kPadding;
		min_y -= kPadding;
		max_x += kPadding;
		max_y += kPadding;
		if (max_x - min_x < kMinimumSize) {
			const float center = (min_x + max_x) * 0.5f;
			min_x = center - kMinimumSize * 0.5f;
			max_x = center + kMinimumSize * 0.5f;
		}
		if (max_y - min_y < kMinimumSize) {
			const float center = (min_y + max_y) * 0.5f;
			min_y = center - kMinimumSize * 0.5f;
			max_y = center + kMinimumSize * 0.5f;
		}

		if (highlight_id_ == 0)
			highlight_id_ =
			ModUI::Highlight::enqueue_add_screen_rect(ImVec2(min_x, min_y), ImVec2(max_x, max_y), label.c_str(), style);
		else
			ModUI::Highlight::enqueue_set_screen_rect(highlight_id_, ImVec2(min_x, min_y), ImVec2(max_x, max_y));
		if (highlight_id_ != 0)
			ModUI::Highlight::enqueue_set_label(highlight_id_, label.c_str());
	}

	void RuntimeModel::clear_highlight_renderer_cache() {
		for (Inspect::ObjectHandle& handle : highlight_renderers_)
			Inspect::FreeObjectHandle(handle);
		highlight_renderers_.clear();
	}

	void RuntimeModel::clear_highlight_camera_cache() {
		for (Inspect::ObjectHandle& handle : highlight_cameras_)
			Inspect::FreeObjectHandle(handle);
		highlight_cameras_.clear();
	}

	void RuntimeModel::clear_selection() {
		if (camera_focus_.active())
			restore_focused_camera();
		if (highlight_id_ != 0) {
			ModUI::Highlight::enqueue_remove(highlight_id_);
			highlight_id_ = 0;
		}
		if (highlight_locator_id_ != 0) {
			ModUI::Highlight::enqueue_remove(highlight_locator_id_);
			highlight_locator_id_ = 0;
		}
		selected_ = {};
		Inspect::FreeObjectHandle(selected_handle_);
		clear_locked_members();
		clear_component_cache();
		clear_highlight_renderer_cache();
		working_.selected_instance_id = 0;
		working_.inspector = {};
		working_.camera_focus_active = false;
	}

} // namespace Explorer
