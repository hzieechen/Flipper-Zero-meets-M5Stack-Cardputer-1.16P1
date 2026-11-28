#include "mp3_storage.h"

#include <string.h>
#include <stdio.h>
#include <esp_heap_caps.h>

#define TAG MP3_PLAYER_TAG

bool mp3_storage_playlist_alloc(Mp3Playlist* playlist, size_t capacity) {
    /* Cardputer (ESP32-S3FN8) has NO PSRAM — every byte comes from internal
     * RAM. Keep capacity tight: a second full 128-slot list (8 KB) starves the
     * decoder worker's 8 KB stack allocation. Recordings use 16 slots. */
    playlist->tracks = heap_caps_calloc(capacity, sizeof(Mp3Track), MALLOC_CAP_DEFAULT);
    playlist->capacity = playlist->tracks ? capacity : 0;
    playlist->count = 0;
    return playlist->tracks != NULL;
}

void mp3_storage_playlist_free(Mp3Playlist* playlist) {
    if(playlist->tracks) {
        heap_caps_free(playlist->tracks);
        playlist->tracks = NULL;
    }
    playlist->count = 0;
}

static bool ends_with_ci(const char* name, const char* suffix) {
    size_t nl = strlen(name);
    size_t sl = strlen(suffix);
    if(nl < sl) return false;
    const char* tail = name + (nl - sl);
    for(size_t i = 0; i < sl; i++) {
        char a = tail[i];
        char b = suffix[i];
        if(a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if(b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if(a != b) return false;
    }
    return true;
}

size_t mp3_storage_scan(
    Storage* storage,
    Mp3Playlist* playlist,
    const char* dir,
    const char* ext) {
    if(!playlist->tracks) return 0;
    playlist->count = 0;

    File* dir_handle = storage_file_alloc(storage);
    if(!storage_dir_open(dir_handle, dir)) {
        FURI_LOG_W(TAG, "scan: cannot open %s", dir);
        storage_file_free(dir_handle);
        return 0;
    }

    char name[MP3_PLAYER_NAME_MAX];
    FileInfo info;
    while(playlist->count < playlist->capacity &&
          storage_dir_read(dir_handle, &info, name, sizeof(name))) {
        if(info.flags & FSF_DIRECTORY) continue;
        if(!ends_with_ci(name, ext)) continue;
        strncpy(playlist->tracks[playlist->count].name, name, MP3_PLAYER_NAME_MAX - 1);
        playlist->tracks[playlist->count].name[MP3_PLAYER_NAME_MAX - 1] = '\0';
        playlist->count++;
    }

    storage_dir_close(dir_handle);
    storage_file_free(dir_handle);

    FURI_LOG_I(TAG, "scan: %u tracks in %s (%s)", (unsigned)playlist->count, dir, ext);
    return playlist->count;
}

bool mp3_storage_track_path(
    const Mp3Playlist* playlist,
    int32_t index,
    const char* dir,
    char* out_path,
    size_t out_size) {
    if(index < 0 || (size_t)index >= playlist->count) return false;
    snprintf(
        out_path,
        out_size,
        "%s/%s",
        dir,
        playlist->tracks[index].name);
    return true;
}
