#include "desktop_audio.hpp"
#include "phantomcrash/source_sound.hpp"
#include <stdexcept>

namespace phantomcrash {
std::shared_ptr<const EffectSamples>
AudioMixer::source_sequence(const std::filesystem::path &path) {
  auto result = std::make_shared<EffectSamples>();
  result->sequence = parse_source_sound(json::read_file(path), [&](const std::string &file) {
    const auto source = roots_.asset(file);
    SDL_AudioSpec spec{};
    Uint8 *pcm = nullptr;
    Uint32 length = 0;
    if (!SDL_LoadWAV(source.string().c_str(), &spec, &pcm, &length)) {
      throw std::runtime_error("Missing source sample " + file);
    }
    if (spec.channels != 1) {
      SDL_free(pcm);
      throw std::runtime_error("Source sequence requires mono samples");
    }
    const SDL_AudioSpec target{SDL_AUDIO_F32, 1, spec.freq};
    Uint8 *converted = nullptr;
    int size = 0;
    const bool ok =
        SDL_ConvertAudioSamples(&spec, pcm, static_cast<int>(length), &target, &converted, &size);
    SDL_free(pcm);
    if (!ok) {
      throw std::runtime_error("Source sample conversion failed " + file);
    }
    SourceSoundSample sample;
    sample.rate = spec.freq;
    const auto *floats = reinterpret_cast<const float *>(converted);
    sample.pcm.assign(floats, floats + size / static_cast<int>(sizeof(float)));
    SDL_free(converted);
    return sample;
  });
  SDL_Log("Source sound sequence: %zu voices with authored sample loops and ADSR",
          result->sequence->notes.size());
  return result;
}
} // namespace phantomcrash
