// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "explorer_model.h"
#include "model_shared.h"
#include "hooks/render_imgui_hook.h"
#include "png_writer.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace URK::Unity;

namespace Explorer {
	namespace {
		// Textures beyond this side length are refused rather than downscaled -
		// keeps the CPU/GPU copy this path makes bounded.
		constexpr int kMaxTexturePreviewDimension = 2048;
		// Frames an outgoing SRV is kept alive before Release(): the model thread
		// that decides to swap or close a preview may not be the thread still
		// consuming last frame's ImGui draw data.
		constexpr int kPendingReleaseFrameDelay = 4;

		constexpr TypeRef kRenderTextureType{"", "UnityEngine", "RenderTexture"};
		constexpr TypeRef kGraphicsType{"", "UnityEngine", "Graphics"};

		struct DecodedImage {
			bool ok = false;
			int width = 0;
			int height = 0;
			std::vector<std::uint8_t> pixels; // RGBA8, row-major, top-to-bottom
			std::string name;
			std::string error;
		};

		// Mirrors Object::CallExact, but for a static method - this SDK has no
		// public helper for "call a static method by exact overload", and Graphics.Blit
		// has multiple same-arity overloads (Blit(Texture,RenderTexture) vs.
		// Blit(Texture,Material)) that inference-by-arity alone can't disambiguate.
		template <class Ret = void, class... Args>
		Ret invoke_static_exact(TypeRef type, std::string_view methodName,
								const std::vector<const char*>& parameterTypeNames, Args&&... args) {
			detail::clear_error();
			const void* klass = type.resolve_class();
			if (!klass) {
				detail::set_error("invoke_static_exact failed: class not found");
				return detail::from_result<Ret>(nullptr);
			}
			const void* method = detail::Backend::find_method_exact(klass, methodName, parameterTypeNames);
			if (!method) {
				detail::set_error("invoke_static_exact failed: method not found: " + std::string(methodName));
				detail::append_backend_error();
				return detail::from_result<Ret>(nullptr);
			}
			auto pack = std::tuple<detail::Arg<std::remove_cvref_t<Args>>...>(
				detail::Arg<std::remove_cvref_t<Args>>(std::forward<Args>(args))...);
			std::array<void*, sizeof...(Args)> argv{};
			std::size_t index = 0;
			bool valid = true;
			std::apply([&](auto&... entry) { ((valid = valid && entry.valid, argv[index++] = entry.ptr), ...); }, pack);
			if (!valid) {
				detail::set_error("invoke_static_exact failed: argument marshaling failed");
				return detail::from_result<Ret>(nullptr);
			}
			void* result = nullptr;
			void* exception = nullptr;
			if (!detail::Backend::runtime_invoke(method, nullptr, argv.data(), &result, &exception) || exception) {
				detail::set_error("invoke_static_exact failed: runtime_invoke exception in " + std::string(methodName));
				return detail::from_result<Ret>(nullptr);
			}
			return detail::from_result<Ret>(result);
		}

		// Mirrors GameObject::Create()'s low-level constructor-invocation pattern,
		// generalized to arbitrary typed arguments via the same Arg<T> marshaling
		// Object::Call already uses.
		template <class... Args>
		Object construct_object(TypeRef type, const std::vector<const char*>& ctorParameterTypeNames, Args&&... args) {
			detail::clear_error();
			const void* klass = type.resolve_class();
			if (!klass) {
				detail::set_error("construct_object failed: class not found");
				return {};
			}
			void* instance = detail::Backend::object_new(klass);
			if (!instance) {
				detail::set_error("construct_object failed: object allocation failed");
				detail::append_backend_error();
				return {};
			}
			const void* ctor = detail::Backend::find_method_exact(klass, ".ctor", ctorParameterTypeNames);
			if (!ctor) {
				detail::set_error("construct_object failed: constructor not found");
				detail::append_backend_error();
				return {};
			}
			auto pack = std::tuple<detail::Arg<std::remove_cvref_t<Args>>...>(
				detail::Arg<std::remove_cvref_t<Args>>(std::forward<Args>(args))...);
			std::array<void*, sizeof...(Args)> argv{};
			std::size_t index = 0;
			bool valid = true;
			std::apply([&](auto&... entry) { ((valid = valid && entry.valid, argv[index++] = entry.ptr), ...); }, pack);
			if (!valid) {
				detail::set_error("construct_object failed: argument marshaling failed");
				return {};
			}
			void* exception = nullptr;
			if (!detail::Backend::runtime_invoke(ctor, instance, argv.data(), nullptr, &exception) || exception) {
				detail::set_error("construct_object failed: constructor threw or could not be invoked");
				return {};
			}
			return Object{instance};
		}

		void flip_rows_vertically(std::vector<std::uint8_t>& pixels, int width, int height) {
			const std::size_t stride = static_cast<std::size_t>(width) * 4;
			std::vector<std::uint8_t> row(stride);
			for (int y = 0; y < height / 2; ++y) {
				std::uint8_t* top = pixels.data() + static_cast<std::size_t>(y) * stride;
				std::uint8_t* bottom = pixels.data() + static_cast<std::size_t>(height - 1 - y) * stride;
				std::memcpy(row.data(), top, stride);
				std::memcpy(top, bottom, stride);
				std::memcpy(bottom, row.data(), stride);
			}
		}

		// GetPixels32() only works when the texture keeps a CPU-side copy
		// (isReadable). Most shipped game art doesn't, so this blits the texture
		// into a scratch RenderTexture and reads that back instead - a GPU-side
		// copy works regardless of the source's CPU readability.
		bool read_texture_via_blit(Texture2D texture, int width, int height, void*& outPixelArray,
								   Object& outScratchTexture, std::string& error) {
			Object renderTexture = construct_object(kRenderTextureType, {"System.Int32", "System.Int32", "System.Int32"},
													width, height, 0);
			if (!renderTexture) {
				error = "could not create a scratch RenderTexture for readback";
				return false;
			}
			invoke_static_exact<void>(kGraphicsType, "Blit", {"UnityEngine.Texture", "UnityEngine.RenderTexture"},
									  Object{texture.handle()}, renderTexture);
			if (const char* blitError = last_error(); blitError && blitError[0]) {
				error = std::string("Graphics.Blit failed: ") + blitError;
				Object::Destroy(renderTexture);
				return false;
			}
			invoke_static_exact<void>(kRenderTextureType, "set_active", {"UnityEngine.RenderTexture"}, renderTexture);
			// TextureFormat.RGBA32 == 4; an enum constructor argument is ABI-identical
			// to the plain int32 Arg<int> below sends, so this is safe without a
			// dedicated enum wrapper.
			Object scratchTexture = construct_object(
				Texture2DType, {"System.Int32", "System.Int32", "UnityEngine.TextureFormat", "System.Boolean"}, width,
				height, 4, false);
			if (!scratchTexture) {
				invoke_static_exact<void>(kRenderTextureType, "set_active", {"UnityEngine.RenderTexture"}, Object{});
				Object::Destroy(renderTexture);
				error = "could not create a scratch Texture2D for readback";
				return false;
			}
			const Rect fullRect{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};
			scratchTexture.CallExact<void>("ReadPixels", {"UnityEngine.Rect", "System.Int32", "System.Int32"}, fullRect,
										   0, 0);
			const bool readFailed = [] { const char* e = last_error(); return e && e[0]; }();
			std::string readError;
			if (readFailed)
				readError = last_error();
			invoke_static_exact<void>(kRenderTextureType, "set_active", {"UnityEngine.RenderTexture"}, Object{});
			Object::Destroy(renderTexture);
			if (readFailed) {
				Object::Destroy(scratchTexture);
				error = "ReadPixels failed: " + readError;
				return false;
			}
			Texture2D{scratchTexture.handle()}.Apply(false, false);
			outPixelArray = scratchTexture.Call<void*>("GetPixels32");
			if (const char* pixelsError = last_error(); pixelsError && pixelsError[0]) {
				Object::Destroy(scratchTexture);
				error = std::string("GetPixels32 failed on the readback copy: ") + pixelsError;
				return false;
			}
			outScratchTexture = scratchTexture;
			return true;
		}

		// Bottom-origin (Unity convention), uncropped, full-size RGBA8 buffer.
		bool decode_full_texture(Texture2D texture, int width, int height, std::vector<std::uint8_t>& outPixels,
								 std::string& error) {
			void* pixelArray = nullptr;
			Object scratchTexture{};
			bool usedScratch = false;
			if (texture.isReadable()) {
				pixelArray = texture.Call<void*>("GetPixels32");
				if (const char* pixelsError = last_error(); pixelsError && pixelsError[0]) {
					error = std::string("GetPixels32 failed: ") + pixelsError;
					return false;
				}
			}
			else if (read_texture_via_blit(texture, width, height, pixelArray, scratchTexture, error)) {
				usedScratch = true;
			}
			else {
				return false;
			}
			const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
			outPixels.assign(pixelCount * 4, 0);
			std::size_t copied = 0;
			const bool readOk = Inspect::ReadRawArrayBytes(pixelArray, 4, pixelCount, outPixels.data(), copied) &&
				copied == pixelCount;
			if (usedScratch)
				Object::Destroy(scratchTexture);
			if (!readOk) {
				error = "could not read pixel data from the managed array";
				return false;
			}
			return true;
		}

		DecodedImage decode_image_source(Object source) {
			DecodedImage out{};
			const std::string runtimeClass = source.runtime_class_name();
			const bool isSprite = URK::Unity::detail::normalized_type_name(runtimeClass) == "unityengine.sprite";
			Texture2D texture{};
			Rect cropRect{};
			bool crop = false;
			if (isSprite) {
				Object spriteTexture = source.GetProperty<Object>("texture");
				if (!spriteTexture) {
					out.error = "sprite has no source texture";
					return out;
				}
				texture = Texture2D{spriteTexture.handle()};
				cropRect = source.GetProperty<Rect>("rect");
				crop = true;
			}
			else {
				texture = Texture2D{source.handle()};
			}
			const int fullWidth = texture.width();
			const int fullHeight = texture.height();
			if (fullWidth <= 0 || fullHeight <= 0) {
				out.error = "invalid dimensions";
				return out;
			}
			if (fullWidth > kMaxTexturePreviewDimension || fullHeight > kMaxTexturePreviewDimension) {
				out.error = "source texture " + std::to_string(fullWidth) + "x" + std::to_string(fullHeight) +
					" exceeds the " + std::to_string(kMaxTexturePreviewDimension) + "x" +
					std::to_string(kMaxTexturePreviewDimension) + " preview limit";
				return out;
			}
			std::vector<std::uint8_t> fullPixels;
			if (!decode_full_texture(texture, fullWidth, fullHeight, fullPixels, out.error))
				return out;
			if (crop) {
				const int cropX = std::clamp(static_cast<int>(cropRect.x + 0.5f), 0, fullWidth);
				const int cropY = std::clamp(static_cast<int>(cropRect.y + 0.5f), 0, fullHeight);
				const int cropWidth = std::clamp(static_cast<int>(cropRect.width + 0.5f), 0, fullWidth - cropX);
				const int cropHeight = std::clamp(static_cast<int>(cropRect.height + 0.5f), 0, fullHeight - cropY);
				if (cropWidth <= 0 || cropHeight <= 0) {
					out.error = "sprite rect is empty or outside its texture";
					return out;
				}
				std::vector<std::uint8_t> cropped(static_cast<std::size_t>(cropWidth) * cropHeight * 4);
				const std::size_t fullStride = static_cast<std::size_t>(fullWidth) * 4;
				const std::size_t cropStride = static_cast<std::size_t>(cropWidth) * 4;
				for (int row = 0; row < cropHeight; ++row) {
					const std::uint8_t* source_row =
						fullPixels.data() + static_cast<std::size_t>(cropY + row) * fullStride +
						static_cast<std::size_t>(cropX) * 4;
					std::memcpy(cropped.data() + static_cast<std::size_t>(row) * cropStride, source_row, cropStride);
				}
				out.pixels = std::move(cropped);
				out.width = cropWidth;
				out.height = cropHeight;
			}
			else {
				out.pixels = std::move(fullPixels);
				out.width = fullWidth;
				out.height = fullHeight;
			}
			flip_rows_vertically(out.pixels, out.width, out.height);
			const std::string name = source.name();
			out.name = name.empty() ? (isSprite ? "Sprite" : "Texture2D") : name;
			out.ok = true;
			return out;
		}

		std::filesystem::path module_directory() {
			static int anchor = 0;
			HMODULE module = nullptr;
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
									reinterpret_cast<LPCWSTR>(&anchor), &module))
				return {};
			std::array<wchar_t, 32768> path{};
			const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
			if (length == 0 || length >= path.size())
				return {};
			return std::filesystem::path(path.data(), path.data() + length).parent_path();
		}

		std::string sanitize_filename(std::string_view name) {
			std::string out(name);
			for (char& ch : out) {
				if (ch == '\\' || ch == '/' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' ||
					ch == '>' || ch == '|')
					ch = '_';
			}
			return out.empty() ? "export" : out;
		}

		std::string timestamp_suffix() {
			const auto now = std::chrono::system_clock::now();
			const std::time_t time = std::chrono::system_clock::to_time_t(now);
			std::tm local{};
			localtime_s(&local, &time);
			std::ostringstream text;
			text << std::put_time(&local, "%Y%m%d_%H%M%S");
			return text.str();
		}
	} // namespace

	void RuntimeModel::preview_texture(const Command& command) {
		const auto found = reference_handles_.find(command.reference_token);
		if (found == reference_handles_.end()) {
			working_.texture_preview.status = "Texture preview failed: the referenced texture is no longer available";
			set_status(working_.texture_preview.status);
			return;
		}
		const Object source = Inspect::ResolveObjectHandle(found->second);
		if (!source) {
			working_.texture_preview.status = "Texture preview failed: the texture was released";
			set_status(working_.texture_preview.status);
			return;
		}
		const DecodedImage decoded = decode_image_source(source);
		if (!decoded.ok) {
			working_.texture_preview.status = "Texture preview failed: " + decoded.error;
			set_status(working_.texture_preview.status);
			return;
		}
		auto* device = static_cast<ID3D11Device*>(ModRenderHook::dx11_device());
		if (!device) {
			working_.texture_preview.status =
				"Texture preview failed: no DX11 device is available - this game may render through DX12 or "
				"OpenGL, which texture preview does not support yet";
			set_status(working_.texture_preview.status);
			return;
		}
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = static_cast<UINT>(decoded.width);
		desc.Height = static_cast<UINT>(decoded.height);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA initial{};
		initial.pSysMem = decoded.pixels.data();
		initial.SysMemPitch = static_cast<UINT>(decoded.width) * 4;
		ID3D11Texture2D* gpu_texture = nullptr;
		if (FAILED(device->CreateTexture2D(&desc, &initial, &gpu_texture)) || !gpu_texture) {
			working_.texture_preview.status = "Texture preview failed: CreateTexture2D failed";
			set_status(working_.texture_preview.status);
			return;
		}
		ID3D11ShaderResourceView* srv = nullptr;
		const HRESULT srv_result = device->CreateShaderResourceView(gpu_texture, nullptr, &srv);
		gpu_texture->Release();
		if (FAILED(srv_result) || !srv) {
			working_.texture_preview.status = "Texture preview failed: CreateShaderResourceView failed";
			set_status(working_.texture_preview.status);
			return;
		}
		if (texture_preview_srv_)
			pending_texture_releases_.push_back({texture_preview_srv_, kPendingReleaseFrameDelay});
		texture_preview_srv_ = srv;
		working_.texture_preview.active = true;
		working_.texture_preview.reference_token = command.reference_token;
		working_.texture_preview.texture_name = decoded.name;
		working_.texture_preview.width = decoded.width;
		working_.texture_preview.height = decoded.height;
		working_.texture_preview.status.clear();
		working_.texture_preview.srv = srv;
		set_status("Previewing " + decoded.name);
	}

	void RuntimeModel::close_texture_preview() {
		if (texture_preview_srv_)
			pending_texture_releases_.push_back({texture_preview_srv_, kPendingReleaseFrameDelay});
		texture_preview_srv_ = nullptr;
		working_.texture_preview = {};
		set_status("Texture preview closed");
	}

	void RuntimeModel::export_texture_preview(const Command& command) {
		const auto found = reference_handles_.find(command.reference_token);
		if (found == reference_handles_.end()) {
			set_status("Texture export failed: the referenced texture is no longer available");
			return;
		}
		const Object source = Inspect::ResolveObjectHandle(found->second);
		if (!source) {
			set_status("Texture export failed: the texture was released");
			return;
		}
		const DecodedImage decoded = decode_image_source(source);
		if (!decoded.ok) {
			set_status("Texture export failed: " + decoded.error);
			return;
		}
		const std::filesystem::path base = module_directory();
		if (base.empty()) {
			set_status("Texture export failed: Explorer DLL directory could not be resolved");
			return;
		}
		std::error_code error;
		const std::filesystem::path directory = base / L"URK_Exports";
		std::filesystem::create_directories(directory, error);
		if (error) {
			set_status("Texture export failed: export directory could not be created: " + error.message());
			return;
		}
		const std::filesystem::path path =
			directory / (sanitize_filename(decoded.name) + "_" + timestamp_suffix() + ".png");
		std::string writeError;
		if (!PngWriter::write_rgba(path.string(), decoded.pixels.data(), decoded.width, decoded.height, writeError)) {
			set_status("Texture export failed: " + writeError);
			return;
		}
		set_status("Exported " + decoded.name + " to " + path.string());
	}

	void RuntimeModel::reap_pending_texture_releases() {
		for (auto it = pending_texture_releases_.begin(); it != pending_texture_releases_.end();) {
			if (--it->frames_remaining <= 0) {
				static_cast<ID3D11ShaderResourceView*>(it->srv)->Release();
				it = pending_texture_releases_.erase(it);
			}
			else {
				++it;
			}
		}
	}

	void RuntimeModel::release_texture_preview_resources() {
		if (texture_preview_srv_) {
			static_cast<ID3D11ShaderResourceView*>(texture_preview_srv_)->Release();
			texture_preview_srv_ = nullptr;
		}
		for (const PendingGpuRelease& pending : pending_texture_releases_)
			static_cast<ID3D11ShaderResourceView*>(pending.srv)->Release();
		pending_texture_releases_.clear();
		working_.texture_preview = {};
	}

} // namespace Explorer
