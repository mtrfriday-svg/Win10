#ifndef MDEE_FAVORITES_H
#define MDEE_FAVORITES_H

// Your favourite audio links. Edit this list, then push to GitHub to rebuild.
// Each link must be a DIRECT http:// or https:// link to an MP3 file or an MP3 internet-radio stream
// (not a YouTube / Spotify / SoundCloud page). Then say "play <name>" or tap it on the phone page.
//
// Example (replace with real links you checked in a browser):
//   {"lofi radio", "https://example.com/stream/lofi.mp3"},
//   {"my song",    "https://example.com/files/song.mp3"},
struct AudioFavorite {
    const char* name;
    const char* url;
};

static const AudioFavorite kAudioFavorites[] = {
    {"edit-me", "https://example.com/replace-me.mp3"},
};

#endif
