#pragma once

#include "render_settings.hpp"

#include <Geode/loader/Event.hpp>

namespace ffmpeg::events {
namespace impl {
#define DEFAULT_RESULT_ERROR geode::Err("Event was not handled")

    class CreateRecorderEvent : public geode::Event<CreateRecorderEvent, bool(CreateRecorderEvent*)> {
    public:
        CreateRecorderEvent() { m_ptr = nullptr; }
        bool post() { return this->send(this); }
        void setPtr(void* ptr) { m_ptr = ptr; }
        void* getPtr() const { return m_ptr; }
    private:
        void* m_ptr;
    };

    class DeleteRecorderEvent : public geode::Event<DeleteRecorderEvent, bool(DeleteRecorderEvent*)> {
    public:
        DeleteRecorderEvent(void* ptr) { m_ptr = ptr; }
        bool post() { return this->send(this); }
        void* getPtr() const { return m_ptr; }
    private:
        void* m_ptr;
    };

    class InitRecorderEvent : public geode::Event<InitRecorderEvent, bool(InitRecorderEvent*)> {
    public:
        InitRecorderEvent(void* ptr, const RenderSettings* settings) {
            m_ptr = ptr;
            m_renderSettings = settings;
        }

        bool post() { return this->send(this); }
        void setResult(geode::Result<>&& result) { m_result = std::move(result); }
        geode::Result<> getResult() { return m_result; }

        void* getPtr() const { return m_ptr; }

        const RenderSettings& getRenderSettings() const { return *m_renderSettings; }

    private:
        const RenderSettings* m_renderSettings;
        void* m_ptr;
        geode::Result<> m_result = DEFAULT_RESULT_ERROR;
    };

    class StopRecorderEvent : public geode::Event<StopRecorderEvent, bool(StopRecorderEvent*)> {
    public:
        StopRecorderEvent(void* ptr) { m_ptr = ptr; }
        bool post() { return this->send(this); }
        void* getPtr() const { return m_ptr; }
    private:
        void* m_ptr;
    };

    struct Dummy {};

    class GetWriteFrameFunctionEvent : public geode::Event<GetWriteFrameFunctionEvent, bool(GetWriteFrameFunctionEvent*)> {
    public:
        using writeFrame_t = geode::Result<>(Dummy::*)(std::vector<uint8_t> const&);
        GetWriteFrameFunctionEvent() = default;

        bool post() { return this->send(this); }
        void setFunction(writeFrame_t function) { m_function = function; }
        writeFrame_t getFunction() const { return m_function; }
    private:
        writeFrame_t m_function = nullptr;
    };

    class CodecRecorderEvent : public geode::Event<CodecRecorderEvent, bool(CodecRecorderEvent*)> {
    public:
        CodecRecorderEvent() = default;

        bool post() { return this->send(this); }
        void setCodecs(std::vector<std::string>&& codecs) { m_codecs = std::move(codecs); }
        const std::vector<std::string>& getCodecs() const { return m_codecs; }
    private:
        std::vector<std::string> m_codecs;
    };

    class MixVideoAudioEvent : public geode::Event<MixVideoAudioEvent, bool(MixVideoAudioEvent*)> {
    public:
        MixVideoAudioEvent(const std::filesystem::path& videoFile, const std::filesystem::path& audioFile, const std::filesystem::path& outputMp4File) {
            m_videoFile = &videoFile;
            m_audioFile = &audioFile;
            m_outputMp4File = &outputMp4File;
        }

        bool post() { return this->send(this); }
        void setResult(geode::Result<>&& result) { m_result = std::move(result); }
        geode::Result<> getResult() { return m_result; }

        std::filesystem::path const& getVideoFile() const { return *m_videoFile; }
        std::filesystem::path const& getAudioFile() const { return *m_audioFile; }
        std::filesystem::path const& getOutputMp4File() const { return *m_outputMp4File; }

    private:
        const std::filesystem::path* m_videoFile;
        const std::filesystem::path* m_audioFile;
        const std::filesystem::path* m_outputMp4File;
        geode::Result<> m_result = DEFAULT_RESULT_ERROR;
    };

    class MixVideoRawEvent : public geode::Event<MixVideoRawEvent, bool(MixVideoRawEvent*)> {
    public:
        MixVideoRawEvent(const std::filesystem::path& videoFile, const std::vector<float>& raw, const std::filesystem::path& outputMp4File) {
            m_videoFile = &videoFile;
            m_raw = &raw;
            m_outputMp4File = &outputMp4File;
        }

        bool post() { return this->send(this); }
        void setResult(const geode::Result<>& result) { m_result = geode::Result(result); }
        geode::Result<> getResult() { return m_result; }

        std::filesystem::path const& getVideoFile() const { return *m_videoFile; }
        std::vector<float> const& getRaw() const { return *m_raw; }
        std::filesystem::path const& getOutputMp4File() const { return *m_outputMp4File; }

    private:
        const std::filesystem::path* m_videoFile;
        const std::vector<float>* m_raw;
        const std::filesystem::path* m_outputMp4File;
        geode::Result<> m_result = DEFAULT_RESULT_ERROR;
    };
#undef DEFAULT_RESULT_ERROR
}

class Recorder {
public:
    Recorder() {
        impl::CreateRecorderEvent createEvent;
        createEvent.post();
        m_ptr = static_cast<impl::Dummy*>(createEvent.getPtr());
    }

    ~Recorder() {
        impl::DeleteRecorderEvent deleteEvent(m_ptr);
        deleteEvent.post();
    }

    bool isValid() const { return m_ptr != nullptr; }

    geode::Result<> init(RenderSettings const& settings) {
        impl::InitRecorderEvent initEvent(m_ptr, &settings);
        initEvent.post();
        return initEvent.getResult();
    }

    void stop() {
        impl::StopRecorderEvent(m_ptr).post();
    }

    geode::Result<> writeFrame(const std::vector<uint8_t>& frameData) {
        static auto writeFrame = [] {
            impl::GetWriteFrameFunctionEvent event;
            event.post();
            return event.getFunction();
        }();
        if (!writeFrame) return geode::Err("Failed to call writeFrame function.");
        return std::invoke(writeFrame, m_ptr, frameData);
    }

    static std::vector<std::string> getAvailableCodecs() {
        impl::CodecRecorderEvent codecEvent;
        codecEvent.post();
        return codecEvent.getCodecs();
    }
private:
    impl::Dummy* m_ptr = nullptr;
};

class AudioMixer {
public:
    AudioMixer() = delete;

    static geode::Result<> mixVideoAudio(std::filesystem::path const& videoFile, std::filesystem::path const& audioFile, std::filesystem::path const& outputMp4File) {
        impl::MixVideoAudioEvent mixEvent(videoFile, audioFile, outputMp4File);
        mixEvent.post();
        return mixEvent.getResult();
    }

    static geode::Result<> mixVideoRaw(std::filesystem::path const& videoFile, const std::vector<float>& raw, const std::filesystem::path& outputMp4File) {
        impl::MixVideoRawEvent mixEvent(videoFile, raw, outputMp4File);
        mixEvent.post();
        return mixEvent.getResult();
    }
};

}
