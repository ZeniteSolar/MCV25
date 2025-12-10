/**
 * File: audio.h
 * Autor: Victor Lompa Schwider
 * 
 * Este arquivo declara as funções e variáveis relacionadas à captura
 * e processamento de áudio utilizando a biblioteca ALSA. Ele define
 * constantes, estruturas e protótipos de funções necessárias para
 * inicializar o dispositivo de áudio, capturar dados de áudio, realizar
 * downsampling e normalização, e preparar os dados para serem usados
 * pelo Edge Impulse SDK e Vosk.
 */

#ifndef AUDIO_H
#define AUDIO_H

#include "edge_impulse.h"
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <alsa/asoundlib.h>

// Usar signal_t do namespace ei
using signal_t = ei::signal_t;

// Debug Includes para salvar arquivos WAV 
// Remover da build final junto da função save_wav
#include "/usr/include/sndfile.h"
#include <iomanip>
#include <sstream>

// Constantes usadas globalmente
#define SAMPLE_LENGTH       EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE
#define ALSA_SAMPLE_RATE    44100   // Frequência de captura do ALSA
#define TARGET_SAMPLE_RATE  16000   // Vosk e EI usam 16kHz
#define CHANNELS            1
#define PCM_DEVICE          "plughw:2,0"  // Dispositivo de entrada (microfone)
#define PCM_OUTPUT_DEVICE   "plughw:2,0"  // Dispositivo de saída (fone/speaker)

// Downsample configs
// 0 -> nearest neighbor (mais rápido)
// 1 -> interpolação linear (melhor qualidade)
#define DOWNSAMPLE_QUALITY  0       // Habilita downsample com melhor qualidade

// Reconnect configs
#define MAX_ATTEMPTS        10      // Tentativa para conectar ao microfone
#define DELAY               5000    // ms de delay entre tentativas

// Debug
#define SAVE_TEST_WAV_RAW   0       // Salva arquivos WAV para debug antes do processamento
#define SAVE_TEST_WAV_DS    0       // Salva arquivos WAV para debug depois do downsample
#define LOGS_ERRO_AUDIO     0       // Habilita ou desabilita logs de erro e de aviso
#define LOGS_INFO_AUDIO     0       // Habilita ou desabilita logs informativos

class Audio {
public:
    // Construtor e destrutor
    Audio();
    ~Audio();

    // Métodos de inicialização
    snd_pcm_t* audio_init();

    // Métodos de captura e processamento
    signal_t get_audio(snd_pcm_t* audio, std::vector<int16_t>* raw_samples_ptr);
    void get_audio_raw(snd_pcm_t* audio, std::vector<int16_t>* raw, size_t len);
    void downsample(const std::vector<int16_t>& input, std::vector<int16_t>& output);
    void normalize(const int16_t* input, size_t length, std::vector<float>& output);
    void normalize(const std::vector<int16_t>& input, std::vector<float>& output);
    std::vector<float> normalize(const std::vector<int16_t>& input);
    bool check_constant_signal(const int16_t* buffer, size_t size);
    bool check_constant_signal(const float* buffer, size_t size);
    int get_signal_audio_data(size_t offset, size_t length, float *out_ptr);
    void save_wav(const std::string &filename, const std::vector<int16_t> &samples, int sample_rate);
    void run_audio(const std::string &filename);

    // Getters para os buffers (interface de leitura)
    std::vector<float>* get_float_samples();
    std::vector<int16_t>* get_converted_samples();    
    std::vector<int16_t>* get_raw_samples();

private:

    // Variáveis de áudio
    std::vector<float> float_samples;
    std::vector<int16_t> converted_samples;
    std::vector<int16_t> raw_samples;
};

#endif
