#include <string.h>
#include <kernel.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "include/sound.h"

static bool adpcm_started = false;
static bool audsrv_started = false;

void sound_setvolume(int volume) {
    if (!audsrv_started) {
        if (audsrv_init() == 0) {
            audsrv_started = true;
        } else {
            printf("[AUDSRV] Error: Failed to initialize audsrv\n");
            return;
        }
    }

    audsrv_set_volume(volume);
}

void sound_setformat(int bits, int freq, int channels) {
    if (!audsrv_started) {
        if (audsrv_init() == 0) {
            audsrv_started = true;
        } else {
            printf("[AUDSRV] Error: Failed to initialize audsrv\n");
            return;
        }
    }

    struct audsrv_fmt_t format;
    format.bits = bits;
    format.freq = freq;
    format.channels = channels;
    
    audsrv_set_format(&format);
}

void sound_setadpcmvolume(int slot, int volume) {
    if (!adpcm_started) {
        if (audsrv_adpcm_init() == 0) {
            adpcm_started = true;
        } else {
            printf("[AUDSRV] Error: Failed to initialize ADPCM sub-system\n");
            return;
        }
    }

    audsrv_adpcm_set_volume(slot, volume);
}

audsrv_adpcm_t* sound_loadadpcm(const char* path) {
    if (!adpcm_started) {
        if (audsrv_adpcm_init() == 0) {
            adpcm_started = true;
        } else {
            printf("[AUDSRV] Error: Cannot load ADPCM - driver not started\n");
            return NULL;
        }
    }

    FILE* adpcm = fopen(path, "rb");
    if (!adpcm) {
        printf("[AUDSRV] Error: Could not open file at path: %s\n", path);
        return NULL;
    }

    // Determine file size safely
    fseek(adpcm, 0, SEEK_END);
    int size = ftell(adpcm);
    fseek(adpcm, 0, SEEK_SET);

    if (size <= 0) {
        printf("[AUDSRV] Error: File %s is empty or invalid size (%d)\n", path, size);
        fclose(adpcm);
        return NULL;
    }

    // Allocate memory for sample metadata struct
    audsrv_adpcm_t *sample = (audsrv_adpcm_t *)malloc(sizeof(audsrv_adpcm_t));
    if (!sample) {
        printf("[AUDSRV] Error: Out of memory for ADPCM structure\n");
        fclose(adpcm);
        return NULL;
    }

    // Allocate raw PCM data buffer
    u8* buffer = (u8*)malloc(size);
    if (!buffer) {
        printf("[AUDSRV] Error: Out of memory allocating %d bytes for sample buffer\n", size);
        free(sample);
        fclose(adpcm);
        return NULL;
    }

    // Read full audio buffer
    int bytes_read = fread(buffer, 1, size, adpcm);
    fclose(adpcm);

    if (bytes_read != size) {
        printf("[AUDSRV] Error: Expected %d bytes, only read %d\n", size, bytes_read);
        free(buffer);
        free(sample);
        return NULL;
    }

    // Pass buffer to audsrv engine
    if (audsrv_load_adpcm(sample, buffer, size) != 0) {
        printf("[AUDSRV] Error: audsrv_load_adpcm failed for %s\n", path);
        free(buffer);
        free(sample);
        return NULL;
    }

    return sample;
}

void sound_playadpcm(int slot, audsrv_adpcm_t *sample) {
    if (!adpcm_started) {
        if (audsrv_adpcm_init() == 0) {
            adpcm_started = true;
        } else {
            printf("[AUDSRV] Error: ADPCM driver not running\n");
            return;
        }
    }

    if (!sample) {
        printf("[AUDSRV] Warning: Attempted to play NULL sample in slot %d\n", slot);
        return;
    }

    audsrv_ch_play_adpcm(slot, sample);
}
