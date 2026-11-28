#ifndef MP3_STORAGE_H
#define MP3_STORAGE_H

#include "mp3_player.h"
#include <storage/storage.h>

/* Allocate the playlist's track array in internal RAM with `capacity` slots.
 * Use a small capacity for recordings to keep heap free for decoder stacks. */
bool mp3_storage_playlist_alloc(Mp3Playlist* playlist, size_t capacity);

/* Free the playlist's track array. */
void mp3_storage_playlist_free(Mp3Playlist* playlist);

/* Scan `dir` non-recursively for files ending with `ext` (case-insensitive)
 * and fill the playlist. Returns the number of tracks added (capped at the
 * playlist's capacity). The caller decides whether to create `dir` first. */
size_t mp3_storage_scan(
    Storage* storage,
    Mp3Playlist* playlist,
    const char* dir,
    const char* ext);

/* Build the full path for the given track index in `dir`.
 * out_path must hold at least 256 bytes. Returns false on bad index. */
bool mp3_storage_track_path(
    const Mp3Playlist* playlist,
    int32_t index,
    const char* dir,
    char* out_path,
    size_t out_size);

#endif /* MP3_STORAGE_H */
