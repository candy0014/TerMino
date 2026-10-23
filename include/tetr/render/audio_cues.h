#ifndef TETR_RENDER_AUDIO_CUES_H
#define TETR_RENDER_AUDIO_CUES_H
#include <vector>
namespace Render {
enum class AudioCue {Piece,Position,Clear,Height};
// Nonblocking tonal cues; initialized only by the audio challenge.
std::vector<double> cue_frequencies(AudioCue cue,int value,int extra=0);
void play_audio_cue(AudioCue cue,int value,int extra=0);
void play_audio_heights(const std::vector<int>& heights);
void shutdown_audio();
}
#endif
