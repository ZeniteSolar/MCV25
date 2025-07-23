#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include <iostream>
#include <unistd.h>
#include <alsa/asoundlib.h>
#include <vector>
#include <cstring>

#define AUDIO_DEVICE "default"
#define SAMPLE_RATE 16000
#define SAMPLE_LENGTH EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE

static std::vector<float> audio_frame;
static snd_pcm_t *pcm_handle = nullptr;

// === Callback de leitura para Edge Impulse ===
int audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    if (offset + length > audio_frame.size()) return -1;
    memcpy(out_ptr, audio_frame.data() + offset, length * sizeof(float));
    return 0;
}

// === Inicializa o microfone uma vez ===
bool inicializar_microfone() {
    int err;
    snd_pcm_hw_params_t *params;

    if ((err = snd_pcm_open(&pcm_handle, AUDIO_DEVICE, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        std::cerr << "Erro ao abrir dispositivo de áudio: " << snd_strerror(err) << std::endl;
        return false;
    }

    snd_pcm_hw_params_malloc(&params);
    snd_pcm_hw_params_any(pcm_handle, params);
    snd_pcm_hw_params_set_access(pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm_handle, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm_handle, params, 1);
    snd_pcm_hw_params_set_rate(pcm_handle, params, SAMPLE_RATE, 0);
    snd_pcm_hw_params(pcm_handle, params);
    snd_pcm_hw_params_free(params);
    snd_pcm_prepare(pcm_handle);

    return true;
}

// === Captura 1 frame de áudio e preenche signal_t ===
bool get_audio_frame_signal(signal_t *signal) {
    short buffer[SAMPLE_LENGTH];
    int err = snd_pcm_readi(pcm_handle, buffer, SAMPLE_LENGTH);
    if (err != SAMPLE_LENGTH) {
        std::cerr << "Erro na leitura do microfone: " << snd_strerror(err) << std::endl;
        return false;
    }

    audio_frame.resize(SAMPLE_LENGTH);
    for (size_t i = 0; i < SAMPLE_LENGTH; i++) {
        audio_frame[i] = buffer[i] / 32768.0f;
    }

    signal->total_length = audio_frame.size();
    signal->get_data = &audio_signal_get_data;
    return true;
}

int main() {
    if (!inicializar_microfone()) return 1;

    signal_t signal;
    ei_impulse_result_t result;
    EI_IMPULSE_ERROR res;

    std::cout << "[INFO] Iniciando detecção contínua da wake-word \"zenira\"...\n";

    while (true) {
        if (!get_audio_frame_signal(&signal)) continue;

        res = run_classifier(&signal, &result, false);
        if (res != EI_IMPULSE_OK) {
            std::cerr << "Erro ao classificar: " << res << std::endl;
            continue;
        }

        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            auto label = result.classification[ix].label;
            auto value = result.classification[ix].value;
            std::cout << label << ": " << value << std::endl;

            if (std::string(label) == "Zenira" && value > 0.8f) {
                std::cout << "[Wake word detectada!]" << std::endl;
            }
        }

        usleep(50000);
    }

    snd_pcm_close(pcm_handle);
    return 0;
}

