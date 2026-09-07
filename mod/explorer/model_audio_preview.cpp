// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "explorer_model.h"
#include "model_shared.h"
#include "wav_writer.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>

using namespace URK::Unity;

namespace Explorer {
	namespace {
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

		// AudioClip.LoadType: DecompressOnLoad = 0, CompressedInMemory = 1, Streaming = 2.
		constexpr int kAudioClipLoadTypeStreaming = 2;
		// AudioDataLoadState: Unloaded = 0, Loading = 1, Loaded = 2, Failed = 3.
		constexpr int kAudioDataLoadStateLoaded = 2;
		constexpr int kAudioDataLoadStateFailed = 3;

		// AudioClip.GetData() only works once the clip's samples are resident in
		// memory (loadState == Loaded); most game audio doesn't reach that state
		// until played or explicitly requested, so this does the actual sample
		// read + WAV write once that's confirmed true. Shared by the immediate
		// path (clip already loaded) and the polled retry (clip was still loading).
		bool write_audio_clip_to_wav(Object clip, std::string& message) {
			const int channels = clip.GetProperty<int>("channels");
			const int frequency = clip.GetProperty<int>("frequency");
			const int samplesPerChannel = clip.GetProperty<int>("samples");
			if (channels <= 0 || frequency <= 0 || samplesPerChannel <= 0) {
				message = "clip reports invalid channels/frequency/samples";
				return false;
			}
			const std::size_t totalSamples =
				static_cast<std::size_t>(samplesPerChannel) * static_cast<std::size_t>(channels);
			// AudioClip.GetData writes into a caller-allocated array; there is no
			// managed API that hands back a new one, so it has to be constructed here.
			const void* floatClass = TypeRef{"", "System", "Single"}.resolve_class();
			if (!floatClass) {
				message = "could not resolve System.Single";
				return false;
			}
			void* sampleArray = detail::Backend::array_new(floatClass, totalSamples);
			if (!sampleArray) {
				message = std::string("could not allocate a managed float array: ") +
					(last_error() ? last_error() : "unknown error");
				return false;
			}
			const bool gotData = clip.CallExact<bool>("GetData", {"System.Single[]", "System.Int32"}, sampleArray, 0);
			if (const char* error = last_error(); error && error[0]) {
				message = std::string("GetData failed: ") + error;
				return false;
			}
			if (!gotData) {
				message = "AudioClip.GetData returned false";
				return false;
			}
			std::vector<float> samples(totalSamples);
			std::size_t copied = 0;
			if (!Inspect::ReadRawArrayBytes(sampleArray, sizeof(float), totalSamples, samples.data(), copied) ||
				copied != totalSamples) {
				message = "could not read sample data from the managed array";
				return false;
			}
			const std::filesystem::path base = module_directory();
			if (base.empty()) {
				message = "Explorer DLL directory could not be resolved";
				return false;
			}
			std::error_code error;
			const std::filesystem::path directory = base / L"URK_Exports";
			std::filesystem::create_directories(directory, error);
			if (error) {
				message = "export directory could not be created: " + error.message();
				return false;
			}
			const std::string clip_name = clip.name();
			const std::string display_name = clip_name.empty() ? "AudioClip" : clip_name;
			const std::filesystem::path path = directory / (sanitize_filename(display_name) + "_" + timestamp_suffix() + ".wav");
			std::string writeError;
			if (!WavWriter::write_float32(path.string(), samples.data(), totalSamples, channels, frequency, writeError)) {
				message = writeError;
				return false;
			}
			message = "Exported " + display_name + " to " + path.string();
			return true;
		}
	} // namespace

	Object RuntimeModel::ensure_audio_preview_source() {
		if (audio_preview_handle_.handle) {
			const Object existing = Inspect::ResolveObjectHandle(audio_preview_handle_);
			if (existing && existing.alive())
				return existing;
			Inspect::FreeObjectHandle(audio_preview_handle_);
		}
		GameObject host = GameObject::New("URK Audio Preview");
		if (!host) {
			capture_last_error("Audio preview host creation");
			return {};
		}
		Object::DontDestroyOnLoad(host);
		AudioSource source = host.AddComponent<AudioSource>();
		if (!source) {
			capture_last_error("Audio preview AudioSource creation");
			Object::Destroy(host);
			return {};
		}
		// Always audible regardless of the listener's position, and never
		// auto-plays on its own - only an explicit Play command should trigger it.
		source.set_playOnAwake(false);
		source.set_spatialBlend(0.0f);
		source.set_loop(false);
		audio_preview_handle_ = Inspect::PinObject(Object{source.handle()}, true);
		if (!audio_preview_handle_.handle) {
			Object::Destroy(host);
			return {};
		}
		return Inspect::ResolveObjectHandle(audio_preview_handle_);
	}

	void RuntimeModel::play_audio_preview(const Command& command) {
		const auto found = reference_handles_.find(command.reference_token);
		if (found == reference_handles_.end()) {
			working_.audio_preview.status = "Audio preview failed: the referenced clip is no longer available";
			set_status(working_.audio_preview.status);
			return;
		}
		const Object clip = Inspect::ResolveObjectHandle(found->second);
		if (!clip) {
			working_.audio_preview.status = "Audio preview failed: the clip was released";
			set_status(working_.audio_preview.status);
			return;
		}
		const Object host = ensure_audio_preview_source();
		if (!host) {
			working_.audio_preview.active = false;
			working_.audio_preview.playing = false;
			working_.audio_preview.status = "Audio preview failed: could not create a preview AudioSource";
			set_status(working_.audio_preview.status);
			return;
		}
		AudioSource source{host.handle()};
		const float volume = std::clamp(command.float_value > 0.0f ? command.float_value : 1.0f, 0.0f, 1.0f);
		source.set_volume(volume);
		source.set_clip(clip);
		if (const char* error = last_error(); error && error[0]) {
			working_.audio_preview.active = false;
			working_.audio_preview.playing = false;
			working_.audio_preview.status = std::string("Audio preview failed: could not assign the clip: ") + error;
			set_status(working_.audio_preview.status);
			return;
		}
		source.Play();
		if (const char* error = last_error(); error && error[0]) {
			working_.audio_preview.active = false;
			working_.audio_preview.playing = false;
			working_.audio_preview.status = std::string("Audio preview failed: Play() failed: ") + error;
			set_status(working_.audio_preview.status);
			return;
		}
		const std::string clip_name = clip.name();
		working_.audio_preview.active = true;
		working_.audio_preview.reference_token = command.reference_token;
		working_.audio_preview.volume = volume;
		working_.audio_preview.clip_name = clip_name.empty() ? "AudioClip" : clip_name;
		// Play() can report success yet Unity never actually starts the source (a
		// paused AudioListener, a muted mixer group, a clip still loading, etc.) -
		// reflect what Unity itself reports instead of assuming success.
		working_.audio_preview.playing = source.isPlaying();
		working_.audio_preview.status = working_.audio_preview.playing
			? std::string{}
			: "Play() reported no error, but Unity's AudioSource.isPlaying is false - check "
			  "AudioListener.pause, the output mixer group, or whether the clip is still loading";
		set_status(working_.audio_preview.playing ? "Playing " + working_.audio_preview.clip_name
		                                          : working_.audio_preview.status);
	}

	void RuntimeModel::export_audio_preview(const Command& command) {
		const auto found = reference_handles_.find(command.reference_token);
		if (found == reference_handles_.end()) {
			set_status("Audio export failed: the referenced clip is no longer available");
			return;
		}
		const Object clip = Inspect::ResolveObjectHandle(found->second);
		if (!clip) {
			set_status("Audio export failed: the clip was released");
			return;
		}
		const int loadType = clip.GetProperty<int>("loadType");
		if (loadType == kAudioClipLoadTypeStreaming) {
			set_status("Audio export failed: this clip streams from disk (AudioClipLoadType.Streaming) - "
				"Unity does not support reading its samples back via GetData");
			return;
		}
		const int loadState = clip.GetProperty<int>("loadState");
		if (loadState == kAudioDataLoadStateLoaded) {
			std::string message;
			if (!write_audio_clip_to_wav(clip, message))
				set_status("Audio export failed: " + message);
			else
				set_status(message);
			return;
		}
		if (loadState == kAudioDataLoadStateFailed) {
			set_status("Audio export failed: the clip's audio data failed to load");
			return;
		}
		// Not loaded yet (most compressed/on-demand clips start this way):
		// request a load and poll for completion over the next few seconds
		// instead of failing outright.
		clip.Call<void>("LoadAudioData");
		pending_audio_export_.active = true;
		Inspect::FreeObjectHandle(pending_audio_export_.clip_handle);
		pending_audio_export_.clip_handle = Inspect::WeakObject(clip);
		pending_audio_export_.attempts_remaining = 300; // ~5s at 60 ticks/s
		const std::string clip_name = clip.name();
		pending_audio_export_.clip_name = clip_name.empty() ? "AudioClip" : clip_name;
		set_status("Loading audio data for " + pending_audio_export_.clip_name + ", export will continue automatically...");
	}

	void RuntimeModel::continue_pending_audio_export() {
		if (!pending_audio_export_.active)
			return;
		const Object clip = pending_audio_export_.clip_handle.handle
			? Inspect::ResolveObjectHandle(pending_audio_export_.clip_handle)
			: Object{};
		if (!clip) {
			pending_audio_export_ = {};
			set_status("Audio export failed: the clip was released while its data was loading");
			return;
		}
		const int loadState = clip.GetProperty<int>("loadState");
		if (loadState == kAudioDataLoadStateLoaded) {
			std::string message;
			const bool ok = write_audio_clip_to_wav(clip, message);
			Inspect::FreeObjectHandle(pending_audio_export_.clip_handle);
			pending_audio_export_ = {};
			set_status(ok ? message : "Audio export failed: " + message);
			return;
		}
		if (loadState == kAudioDataLoadStateFailed) {
			Inspect::FreeObjectHandle(pending_audio_export_.clip_handle);
			pending_audio_export_ = {};
			set_status("Audio export failed: the clip's audio data failed to load");
			return;
		}
		if (--pending_audio_export_.attempts_remaining <= 0) {
			Inspect::FreeObjectHandle(pending_audio_export_.clip_handle);
			set_status("Audio export failed: timed out waiting for " + pending_audio_export_.clip_name +
				"'s audio data to finish loading");
			pending_audio_export_ = {};
		}
	}

	void RuntimeModel::stop_audio_preview() {
		if (audio_preview_handle_.handle) {
			const Object existing = Inspect::ResolveObjectHandle(audio_preview_handle_);
			if (existing)
				AudioSource{existing.handle()}.Stop();
		}
		working_.audio_preview.playing = false;
		set_status("Audio preview stopped");
	}

	void RuntimeModel::refresh_audio_preview() {
		if (!working_.audio_preview.active)
			return;
		const Object existing = audio_preview_handle_.handle ? Inspect::ResolveObjectHandle(audio_preview_handle_) : Object{};
		if (!existing) {
			working_.audio_preview.active = false;
			working_.audio_preview.playing = false;
			return;
		}
		working_.audio_preview.playing = AudioSource{existing.handle()}.isPlaying();
	}

	void RuntimeModel::release_audio_preview() {
		if (audio_preview_handle_.handle) {
			const Object existing = Inspect::ResolveObjectHandle(audio_preview_handle_);
			if (existing) {
				AudioSource source{existing.handle()};
				source.Stop();
				Object::Destroy(source.gameObject());
			}
			Inspect::FreeObjectHandle(audio_preview_handle_);
		}
		working_.audio_preview = {};
	}

} // namespace Explorer
