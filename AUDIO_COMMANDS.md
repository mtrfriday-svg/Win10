# Internet audio (MP3 only, no video) – how to use

The robot streams an **MP3 file or MP3 internet-radio stream** from a **direct http/https link** to its speaker.
It uses about 50 KB of RAM while playing and nothing when stopped. No video code is included.

**Works:** links that end in `.mp3`, or radio streams that are MP3 (Icecast/Shoutcast "audio/mpeg").
**Does NOT work:** YouTube, Spotify, SoundCloud, Apple Music pages, AAC/OGG/FLAC/WAV files, playlists (`.m3u`, `.pls`).
Test a link in Termux first (`pkg install curl`):
```
curl -sIL "https://your.link/song.mp3" | grep -i -E "^HTTP|content-type"
```
You want `HTTP ... 200` and `content-type: audio/mpeg`.

Phone-only tip: put an MP3 you own in your GitHub repo and use
`https://raw.githubusercontent.com/<your-user>/<your-repo>/main/<file>.mp3` (only host audio you have the right to share).
Use the final link, not a short link that redirects; if you see HTTP 301/302/403, find the direct link.

## 1. Phone page (easiest way to enter links)
Open `http://<robot-ip>/` (IP is shown on the OLED when idle) → card **Play internet audio**:
paste the link → **Play**. **Stop** ends it. Your saved favourites appear as buttons under the box.

## 2. Voice (the AI calls these tools)
You cannot dictate a long link by voice, so use favourites or the phone page. Phrases that work:
| Say | Tool used |
|---|---|
| "Play lofi" / "play my song" (a favourite name) | `self.audio_url.play_favorite` |
| "What favourites do you have?" | `self.audio_url.list_favorites` |
| "Stop the music" / "be quiet" | `self.audio_url.stop` |
| "Is music playing?" | `self.audio_url.status` |
| "Play this link: https://…mp3" (if you typed it in the chat) | `self.audio_url.play` |

## 3. Stop it any time
* Tap **Stop** on the phone page, or **click the BOOT button** (first click = stop music).
* Say the wake word or start talking to the robot: the music ends and the robot listens.
  (The robot has no echo cancellation, so very loud music can hide the wake word. Use the button or lower the volume.)
* It never talks over the assistant: it waits (up to 30 s) until the robot is idle, then starts.

## 4. Save your favourite links
Edit `main/boards/mdee-robot/favorites.h` and add lines like
```
    {"lofi radio", "https://your.stream/lofi.mp3"},
    {"my song",    "https://your.site/files/song.mp3"},
```
In Termux (`pkg install nano` once):
```
cd ~/Bot2-main
nano main/boards/mdee-robot/favorites.h
```
Save with **CTRL+O, Enter**, exit with **CTRL+X**. Then rebuild:
```
bash termux_push.sh "new favourites"
```
Download the new `mdee_brain_n16r8_…` artifact from GitHub Actions and flash it. (Favourites are built in, so the
robot needs a new flash to change them. Links typed on the phone page never need a rebuild.)

## 5. What you see and hear
OLED: `Loading: name`, then `Playing: name`. NeoPixels go rainbow and the ears wiggle every few seconds.
Volume is the robot's normal speaker volume (ask: "set volume to 60").

## 6. Messages and fixes
| OLED / reply | Meaning / fix |
|---|---|
| `Audio: cannot open URL` | Wrong link, no internet, or the site blocks devices. Test with `curl`. |
| `Audio: HTTP error 403/404` | Not a direct link. Use the real file/stream address. |
| `Audio: not an MP3 (MP3 links only)` | It is a web page, OGG, WAV or FLAC. Find an MP3 link. |
| `Audio: connection lost` | Wi-Fi dropped or the stream ended. Try again. |
| `not enough free memory` | Wait a few seconds (the robot is busy) and try again. |
| Nothing happens | It waits until the robot stops talking/listening. Check state on the OLED. |
| Stutters | Weak Wi-Fi or a very high-bitrate stream. Choose a 64-128 kbps stream. |
