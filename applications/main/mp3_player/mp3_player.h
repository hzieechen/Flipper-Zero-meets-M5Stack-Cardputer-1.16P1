#ifndef MP3_PLAYER_H
#define MP3_PLAYER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <furi.h>
#include <gui/gui.h>

#define MP3_PLAYER_TAG          "MP3Player"
#define MP3_PLAYER_DATA_DIR     "/ext/apps_data/mp3_player"
#define MP3_RECORDINGS_DIR      "/ext/recordings"
#define MP3_PLAYER_MAX_TRACKS   128
/* Recordings are far fewer than music tracks. Keeping this small avoids a
 * second 8 KB allocation that can exhaust internal RAM when a worker starts. */
#define MP3_RECORDINGS_MAX_TRACKS 16
#define MP3_PLAYER_NAME_MAX     64

/* Which directory a track came from — decides the path prefix and the
 * decoder format (mp3 via libhelix, wav as raw PCM). */
typedef enum {
    Mp3DirMusic,
    Mp3DirRecordings,
} Mp3Dir;

typedef struct {
    char name[MP3_PLAYER_NAME_MAX];
} Mp3Track;

typedef struct {
    Mp3Track* tracks;     /* internal-RAM array */
    size_t    count;
    size_t    capacity;   /* allocated slots, set by playlist_alloc */
} Mp3Playlist;

typedef enum {
    Mp3ViewBrowser,       /* music list, with a "录音机" entry at row 0 */
    Mp3ViewRecordings,    /* recordings list under /ext/recordings */
    Mp3ViewNowPlaying,
} Mp3View;

typedef enum {
    Mp3StateIdle,
    Mp3StatePlaying,
    Mp3StatePaused,
} Mp3PlaybackState;

typedef struct {
    /* shared state */
    FuriMutex*        mutex;
    FuriMessageQueue* event_queue;

    /* playlists: music + recordings */
    Mp3Playlist playlist;
    Mp3Playlist recordings;
    int32_t     selected;            /* browser cursor: -1 = 录音机 entry; 0..n-1 = music */
    int32_t     selected_rec;        /* cursor inside the recordings list */

    /* current view */
    Mp3View view;

    /* playback */
    Mp3PlaybackState playback;
    Mp3Dir          playing_dir;     /* which playlist the current track belongs to */
    int32_t          playing_index;  /* track currently being played (-1 if none) */
    uint32_t         elapsed_ms;     /* updated by decoder thread */
    uint32_t         duration_ms;    /* 0 if unknown */
    uint8_t          volume;         /* 0..100 */
} Mp3App;

typedef enum {
    Mp3EventTypeKey,
    Mp3EventTypeTick,
    Mp3EventTypeTrackEnded,
} Mp3EventType;

typedef struct {
    Mp3EventType type;
    InputEvent   input;
} Mp3Event;

#endif /* MP3_PLAYER_H */
