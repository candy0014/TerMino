#include "tetr/render/audio_cues.h"
#include "tetr/render/terminal_renderer.h"
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__ANDROID__)
#include <SDL3/SDL.h>
#else
#include <dlfcn.h>
#endif
namespace Render {
std::vector<double> cue_frequencies(AudioCue cue,int value,int extra) {
    if(cue==AudioCue::Piece)return {440,330*std::pow(2.0,std::clamp(value,0,6)/12.0),660};
    if(cue==AudioCue::Position)return {220+22.0*std::clamp(value,0,100),440+55.0*(extra%4)};
    if(cue==AudioCue::Height)return {110+11.0*std::clamp(value,0,100)};
    std::vector<double> notes;for(int i=0;i<std::clamp(value,0,8)+1;++i)notes.push_back(440*std::pow(2.0,i/12.0));return notes;
}
namespace {
class AudioWorker {
    std::mutex mutex;std::condition_variable wake;std::deque<std::vector<double>> queue;bool stop=false;std::thread thread;
    void run() {
#ifdef __ANDROID__
        SDL_AudioSpec spec{SDL_AUDIO_S16,1,22050};
        SDL_AudioStream* stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
        if(stream)SDL_ResumeAudioStreamDevice(stream);
#elif !defined(_WIN32)
        // PulseAudio simple API loaded optionally: no new build-time package.
        struct SampleSpec {int format;std::uint32_t rate;std::uint8_t channels;};
        void* library=dlopen("libpulse-simple.so.0",RTLD_LAZY);
        using Open=void*(*)(const char*,const char*,int,const char*,const char*,const SampleSpec*,const void*,const void*,int*);
        using Write=int(*)(void*,const void*,std::size_t,int*);using Close=void(*)(void*);
        auto open=library?reinterpret_cast<Open>(dlsym(library,"pa_simple_new")):nullptr;
        auto write=library?reinterpret_cast<Write>(dlsym(library,"pa_simple_write")):nullptr;
        auto close=library?reinterpret_cast<Close>(dlsym(library,"pa_simple_free")):nullptr;
        SampleSpec spec{3,22050,1};int error=0;
        void* stream=open && write && close?open(nullptr,"TerMino",1,nullptr,"Challenge cues",&spec,nullptr,nullptr,&error):nullptr;
#endif
        for(;;) {
            std::vector<double> notes;
            {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stop || !queue.empty();});if(stop)break;notes=std::move(queue.front());queue.pop_front();}
            for(double frequency:notes) {
#ifdef _WIN32
                Beep(static_cast<DWORD>(frequency),40);
#else
                if(!stream)continue;
                std::vector<std::int16_t> samples(882);
                for(std::size_t i=0;i<samples.size();++i){const double envelope=std::min({1.0,i/80.0,(samples.size()-i)/80.0});samples[i]=static_cast<std::int16_t>(7000*envelope*std::sin(6.283185307179586*frequency*i/22050));}
#ifdef __ANDROID__
                if(SDL_GetAudioStreamQueued(stream)>22050*4)SDL_ClearAudioStream(stream);
                if(!SDL_PutAudioStreamData(stream,samples.data(),int(samples.size()*sizeof(samples[0]))))break;
#else
                if(write(stream,samples.data(),samples.size()*sizeof(samples[0]),&error)<0)break;
#endif
#endif
            }
        }
#ifdef __ANDROID__
        if(stream)SDL_DestroyAudioStream(stream);
#elif !defined(_WIN32)
        if(stream)close(stream);if(library)dlclose(library);
#endif
    }
public:
    AudioWorker():thread([this]{run();}){}
    ~AudioWorker(){shutdown();}
    void shutdown(){{std::lock_guard<std::mutex> lock(mutex);stop=true;}wake.notify_one();if(thread.joinable())thread.join();}
    void push(std::vector<double> notes){{std::lock_guard<std::mutex> lock(mutex);if(queue.size()>8)queue.pop_front();queue.push_back(std::move(notes));}wake.notify_one();}
};
AudioWorker& audio_worker(){static AudioWorker worker;return worker;}
}
void shutdown_audio(){audio_worker().shutdown();}
void play_audio_cue(AudioCue cue,int value,int extra) {
    if(output_suppressed() && !detached_audio_enabled())return;
    audio_worker().push(cue_frequencies(cue,value,extra));
}
void play_audio_heights(const std::vector<int>& heights) {
    if(output_suppressed() && !detached_audio_enabled())return;
    std::vector<double> notes;for(int height:heights)notes.push_back(cue_frequencies(AudioCue::Height,height).front());
    audio_worker().push(std::move(notes));
}
}
