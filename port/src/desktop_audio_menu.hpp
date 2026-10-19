#pragma once

#include "phantomcrash/audio_options.hpp"

namespace phantomcrash {
void *create_audio_menu(void *parent, const AudioOptions &options);
void refresh_audio_menu(void *menu, const AudioOptions &options);
bool audio_menu_command(unsigned int command, AudioOptions &options);
} // namespace phantomcrash
