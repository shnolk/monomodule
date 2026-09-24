// Audio preview, in three parts:
//   PreviewRenderer  renders previews (preview/Preview.h) on a background thread with the emulated DSPs and keeps
//                    recent renders in memory. Nothing is written to disk: a preset renders in ~0.1 s, a six-track
//                    pattern in a fraction of its length.
//   PreviewVoice     the audio-thread side: plays one rendered buffer into float channels at any sample rate. It
//                    starts once a prefill is rendered and reads what the render thread has finished so far (the
//                    render runs several times faster than real time), so a click sounds at once.
//   PreviewPlayer    renderer + voice + the default output device: the Library app's player.
// The plugins own a renderer and a voice and mix the voice into their own output (a plugin has no device).
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <array>
#include <list>
#include <map>
#include <memory>
#include "preview/KitRenderer.h"
#include "preview/Preview.h"

namespace mnm::library {

// One rendered preview: the mix and, for patterns and kits, the six tracks' own outputs. The render thread
// fills the buffers block by block and publishes `ready`; frames below it are complete.
struct PreviewAudio {
    uint32_t frames = 0;
    // The loop region: the last full pass of the pattern, [loopStart, loopEnd). Looping wraps there, at the
    // pattern's musical length, like the unit looping the pattern (its trigs repeat, the last pass's ringing
    // tail is already under the first steps of that pass); the release tail after loopEnd is only heard once
    // playback runs out. loopEnd = 0 means no region (loop the whole buffer).
    uint32_t loopStart = 0, loopEnd = 0;
    bool stems = false;
    std::vector<float> mixL, mixR;
    std::array<std::vector<float>, 6> stemL, stemR;
    std::atomic<uint32_t> ready{0};
    std::atomic<bool> done{false}, failed{false};
    std::string error;
    size_t bytes() const { return (mixL.size() + mixR.size() + (stems ? 12 * mixL.size() : 0)) * sizeof(float); }
};

class PreviewRenderer : private juce::Thread {
public:
    PreviewRenderer();
    ~PreviewRenderer() override;
    // The OS file (the plugins' shared setting); loaded on the render thread at the first preview, reloaded when
    // the path changes. Empty = previews are unavailable (status() says so).
    void setFirmwarePath(const juce::String& path);
    bool hasFirmwarePath() const;
    // The render named `key` at this tempo: from the cache, or started now (`build` is called synchronously when it
    // is not cached). A request for another key supersedes a render in progress.
    std::shared_ptr<PreviewAudio> request(const juce::String& key, double bpm, const std::function<preview::PreviewSpec()>& build);
    bool rendering() const { return m_rendering.load(); }
    juce::String status() const;          // "" or the last problem: no OS file, load failure, render failure
    void clearStatus() { setStatus({}); }
    std::function<void()> onChange;       // called on the render thread when a render ends or the status changes
    void setCacheLimit(size_t bytes) { m_cacheLimit = bytes; }
    void keep(const std::shared_ptr<PreviewAudio>& playing) { m_keep = playing; }   // never evicted

private:
    struct Job { juce::String key; preview::PreviewSpec spec; std::shared_ptr<PreviewAudio> audio; double bpm = 120.0; };
    void run() override;
    void renderJob(Job& job);
    void setStatus(const juce::String& s);
    void trimCache();

    std::map<juce::String, std::shared_ptr<PreviewAudio>> m_cache;   // message thread
    std::list<juce::String> m_lru;                                    // most recent last
    size_t m_cacheLimit = size_t(256) << 20;
    std::shared_ptr<PreviewAudio> m_keep;
    juce::CriticalSection m_jobLock;
    std::unique_ptr<Job> m_pending;
    std::shared_ptr<PreviewAudio> m_current;   // being rendered (its cancel = a new job or exit)
    std::atomic<bool> m_cancel{false}, m_rendering{false};
    juce::WaitableEvent m_jobEvent;
    juce::String m_fwPath;                     // under m_jobLock
    bool m_fwDirty = true;
    std::unique_ptr<fw::Firmware> m_firmware;
    std::unique_ptr<preview::KitRenderer> m_renderer;
    mutable juce::CriticalSection m_statusLock;
    juce::String m_status;
};

class PreviewVoice {
public:
    PreviewVoice();
    // message thread
    void start(std::shared_ptr<PreviewAudio> audio, int stem);   // stem -1 = the mix
    void stop();
    bool active() const;
    double progress() const;
    // Loop: the preview repeats its loop region (PreviewAudio::loopStart/loopEnd: the last pass of the pattern,
    // so the trigs repeat at the pattern's length and the tail is skipped) without a gap, once that region is
    // rendered. Reset by start().
    void setLoop(bool on) { m_loop.store(on); }
    bool loop() const { return m_loop.load(); }
    bool consumeFinished() { return m_finished.exchange(false); }   // true once after the preview played to its end
    // audio thread: adds (or writes, when `replace`) n frames of the preview at `sampleRate` into outL / outR
    // (outR may be null). Returns false when there is nothing to play.
    bool process(float* outL, float* outR, int n, double sampleRate, bool replace);
    void resetRate() { m_interpL.reset(); m_interpR.reset(); }

private:
    mutable juce::SpinLock m_lock;
    std::shared_ptr<PreviewAudio> m_playing;
    int m_stem = -1;
    uint32_t m_pos = 0;
    bool m_started = false;
    juce::LagrangeInterpolator m_interpL, m_interpR;
    std::vector<float> m_padL, m_padR, m_tmpL, m_tmpR;
    std::atomic<uint32_t> m_playPos{0};
    std::atomic<bool> m_finished{false}, m_loop{false};
};

class PreviewPlayer : private juce::AudioIODeviceCallback, private juce::AsyncUpdater {
public:
    PreviewPlayer();
    ~PreviewPlayer() override;
    void setFirmwarePath(const juce::String& path) { m_renderer.setFirmwarePath(path); }
    void setOptions(const preview::PreviewOptions& opt) { m_options = opt; }   // tempo etc.; a change makes the cache stale
    preview::PreviewOptions options() const { return m_options; }

    // Plays a preview. `key` names the render (the catalog id and kind), `build` produces its spec when it is not
    // cached, `stem` = -1 the mix or a track 0-5. Replaces whatever is playing (the caller decides about
    // stop-on-second-click).
    void play(const juce::String& key, const std::function<preview::PreviewSpec()>& build, int stem = -1);
    void stop();
    bool isPlaying() const { return m_playingKey.isNotEmpty(); }
    // Loop the playing preview until it is switched off, stopped or replaced by another preview (play() and stop()
    // reset it).
    void setLoop(bool on) { m_voice.setLoop(on && isPlaying()); if (onChange) onChange(); }
    bool loop() const { return isPlaying() && m_voice.loop(); }
    const juce::String& playingKey() const { return m_playingKey; }
    int playingStem() const { return m_playingStem; }
    double progress() const { return m_voice.progress(); }
    bool rendering() const { return m_renderer.rendering(); }
    juce::String status() const;      // the renderer's, or "no audio output device"
    std::function<void()> onChange;   // message thread: playback started, ended or failed, status changed

private:
    void audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override {}
    void handleAsyncUpdate() override;
    bool ensureDevice();

    PreviewRenderer m_renderer;
    PreviewVoice m_voice;
    juce::AudioDeviceManager m_devices;
    bool m_deviceReady = false, m_deviceTried = false;
    juce::String m_playingKey, m_deviceStatus;
    int m_playingStem = -1;
    std::shared_ptr<PreviewAudio> m_audio;
    preview::PreviewOptions m_options;
    double m_deviceRate = 44100.0;
};

} // namespace mnm::library
