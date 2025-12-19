/**
 * File: audio.cpp
 * Autor: Victor Lompa Schwider
 * 
 * Este arquivo implementa a captura e processamento de áudio.
 * Ele utiliza a biblioteca ALSA para capturar áudio do microfone,
 * realiza downsampling e normalização, e prepara os dados para
 * serem usados pelo Edge Impulse SDK e Vosk.
 */

#include "audio.h"

#ifdef __ARM_NEON__
#include <arm_neon.h>
#endif

#include <cmath>

static const float INT16_RECIPROCAL = 1.0f / 32768.0f;
static uint16_t wav_counter = 0;

/** Construtor da classe Audio
 * Inicializa os vetores com tamanhos apropriados
 * 
 * @return void
 */
Audio::Audio() {
    raw_samples.resize(ALSA_SAMPLE_RATE);      // 44100 amostras
    converted_samples.resize(SAMPLE_LENGTH);    // 16000 amostras
    float_samples.resize(SAMPLE_LENGTH);        // 16000 amostras
}

/** Destrutor da classe Audio. 
 * Libera recursos se necessário
 * 
 * @return void
 */
Audio::~Audio() {
}

/** Inicializa o dispositivo de áudio ALSA e configura os parâmetros necessários.
*
*   @return snd_pcm_t* Ponteiro para o dispositivo de áudio ALSA ou nullptr em caso de erro.
*/
snd_pcm_t* Audio::audio_init() {
    snd_pcm_t *pcm_handle = nullptr;
    snd_pcm_hw_params_t *params = nullptr;
    int err;

#if LOGS_INFO_AUDIO
    std::cout << "[INFO] Inicializando captura de áudio ALSA em \"" << PCM_DEVICE << "\"..." << std::endl;
#endif

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
        err = snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_CAPTURE, 0);
        if (err >= 0) {
#if LOGS_INFO_AUDIO
            std::cout << "[INFO] Microfone aberto na tentativa " << attempt << " de " << MAX_ATTEMPTS << "." << std::endl;
#endif
            
            if ((err = snd_pcm_hw_params_malloc(&params)) < 0) {
#if LOGS_ERRO_AUDIO
                std::cerr << "[ERRO] Falha ao alocar hw_params: " << snd_strerror(err) << std::endl;
#endif
                snd_pcm_close(pcm_handle);
                return nullptr;
            }

            if ((err = snd_pcm_hw_params_any(pcm_handle, params)) < 0 ||
                (err = snd_pcm_hw_params_set_access(pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
                (err = snd_pcm_hw_params_set_format(pcm_handle, params, SND_PCM_FORMAT_S16_LE)) < 0 ||
                (err = snd_pcm_hw_params_set_channels(pcm_handle, params, CHANNELS)) < 0 ||
                (err = snd_pcm_hw_params_set_rate(pcm_handle, params, ALSA_SAMPLE_RATE, 0)) < 0 ||
                (err = snd_pcm_hw_params(pcm_handle, params)) < 0) {

#if LOGS_ERRO_AUDIO
                std::cerr << "[ERRO] Falha ao configurar hw_params: " << snd_strerror(err) << std::endl;
#endif

                snd_pcm_hw_params_free(params);
                snd_pcm_close(pcm_handle);
                return nullptr;
            }

            snd_pcm_hw_params_free(params);

            if ((err = snd_pcm_prepare(pcm_handle)) < 0) {

#if LOGS_ERRO_AUDIO
                std::cerr << "[ERRO] Falha ao preparar PCM: " << snd_strerror(err) << std::endl;
#endif

                snd_pcm_close(pcm_handle);
                return nullptr;
            }

#if LOGS_INFO_AUDIO
            std::cout << "[INFO] Áudio configurado para " << ALSA_SAMPLE_RATE << " Hz, " << CHANNELS << " canal(is), formato S16_LE." << std::endl;
#endif

            return pcm_handle;
        }

#if LOGS_ERRO_AUDIO
        std::cerr << "[AVISO] Tentativa " << attempt << " falhou: " << snd_strerror(err) << std::endl;
#endif

        if (attempt < MAX_ATTEMPTS) {

#if LOGS_INFO_AUDIO
            std::cerr << "[INFO] Aguardando " << DELAY << "ms antes de tentar novamente...\n";
#endif
            usleep(DELAY * 1000);
        }
    }

#if LOGS_ERRO_AUDIO
    std::cerr << "[ERRO] Não foi possível inicializar o microfone após " << MAX_ATTEMPTS << " tentativas.\n";
#endif

    return nullptr;
}

/** Obtém o áudio do dispositivo de captura.
 *
 * @param audio Dispositivo de captura de áudio.
 * @param raw_samples Buffer para armazenar os samples de áudio brutos.
 * @param sample_length Número de samples a serem lidos (padrão: SAMPLE_LENGTH).
 * @param op Operação a ser realizada: 'd' (downsample), 'n' (normalize), 'b' (both).
 *
 * @return Estrutura de sinal contendo os dados de áudio.
 */
signal_t Audio::get_audio(snd_pcm_t* audio, std::vector<int16_t>* raw_samples_ptr) {
    if (!raw_samples_ptr) return {};

    size_t offset = 0;
    std::vector<int16_t>& raw_samples = *raw_samples_ptr;

    // Preenche 1 segundo de áudio bruto
    while (offset < raw_samples.size()) {
        int frames_to_read = raw_samples.size() - offset;
        int err = snd_pcm_readi(audio, raw_samples.data() + offset, frames_to_read);
        if (err < 0) {
#if LOGS_ERRO_AUDIO
            std::cerr << "[ERRO] Falha ao ler dados de áudio: " << snd_strerror(err) << std::endl;
#endif
            snd_pcm_prepare(audio);
            return {}; // sinal inválido
        }
        offset += err;
    }

    // Verifica sinal constante
    if (check_constant_signal(raw_samples.data(), raw_samples.size())) {
#if LOGS_INFO_AUDIO
        std::cerr << "[INFO] Sinal de áudio constante detectado. Ignorando frame.\n";
#endif
        return {};
    }

#if SAVE_TEST_WAV_RAW
    std::stringstream ss1;
    ss1 << "teste_audio_raw_" << std::setw(3) << std::setfill('0') << wav_counter++ << ".wav";
    save_wav(ss1.str(), raw_samples, ALSA_SAMPLE_RATE);
#endif

    // Downsample e normalização conforme opção selecionada
    if(ALSA_SAMPLE_RATE <= TARGET_SAMPLE_RATE) {
#if LOG_AVISO
        std::cerr << "[AVISO] Frequência de entrada é menor ou igual a frequência alvo. Pulando downsample.\n";
#endif
        converted_samples = raw_samples;
    } else {
        downsample(raw_samples, converted_samples);
#if SAVE_TEST_WAV_DS
        std::stringstream ss2;
        ss2 << "teste_audio_ds_" << std::setw(3) << std::setfill('0') << wav_counter++ << ".wav";
        save_wav(ss2.str(), converted_samples, TARGET_SAMPLE_RATE);
#endif
    }

    normalize(converted_samples, float_samples);

#if HIGHPASS_FILTER_ENABLE
    // Aplicar filtro passa-alta para redução de ruído de vento (otimizado para 16kHz)
    highpass_filter(float_samples, HIGHPASS_CUTOFF_FREQ, TARGET_SAMPLE_RATE, HIGHPASS_FILTER_ORDER);
#if SAVE_TEST_WAV_HP
    // Salvar arquivo WAV para debug após filtro passa-alta
    std::stringstream ss3;
    ss3 << "teste_audio_hp_" << std::setw(3) << std::setfill('0') << wav_counter++ << ".wav";
    // Converter float samples de volta para int16_t para salvar
    std::vector<int16_t> hp_int_samples(float_samples.size());
    for (size_t i = 0; i < float_samples.size(); ++i) {
        float clamped = std::max(-1.0f, std::min(1.0f, float_samples[i]));
        hp_int_samples[i] = static_cast<int16_t>(clamped * 32767.0f);
    }
    save_wav(ss3.str(), hp_int_samples, TARGET_SAMPLE_RATE);
#endif
#endif

#if AUDIO_DENOISE_ENABLE
    // Aplicar suavização adaptativa após o filtro
    adaptive_denoise(float_samples);
#if SAVE_TEST_WAV_DN
    // Salvar arquivo WAV para debug após suavização adaptativa
    std::stringstream ss4;
    ss4 << "teste_audio_dn_" << std::setw(3) << std::setfill('0') << wav_counter++ << ".wav";
    // Converter float samples de volta para int16_t para salvar
    std::vector<int16_t> dn_int_samples(float_samples.size());
    for (size_t i = 0; i < float_samples.size(); ++i) {
        float clamped = std::max(-1.0f, std::min(1.0f, float_samples[i]));
        dn_int_samples[i] = static_cast<int16_t>(clamped * 32767.0f);
    }   
    save_wav(ss4.str(), dn_int_samples, TARGET_SAMPLE_RATE);
#endif

#if AUDIO_GAIN_ENABLE
    // Aplicar ganho adaptativo para compensar atenuação dos filtros
    adaptive_gain(float_samples, AUDIO_GAIN_TARGET_RMS);
#endif
#if SAVE_TEST_WAV_AG
    // Salvar arquivo WAV para debug após suavização adaptativa
    std::stringstream ss5;
    ss5 << "teste_audio_ag_" << std::setw(3) << std::setfill('0') << wav_counter++ << ".wav";
    // Converter float samples de volta para int16_t para salvar
    std::vector<int16_t> ag_int_samples(float_samples.size());
    for (size_t i = 0; i < float_samples.size(); ++i) {
        float clamped = std::max(-1.0f, std::min(1.0f, float_samples[i]));
        ag_int_samples[i] = static_cast<int16_t>(clamped * 32767.0f);
    }   
    save_wav(ss5.str(), ag_int_samples, TARGET_SAMPLE_RATE);
#endif
#endif

    // Preenche a estrutura de sinal
    signal_t signal;
    signal.total_length = float_samples.size();
    signal.get_data = std::bind(&Audio::get_signal_audio_data, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    return signal;
}

/** Captura samples brutos do ALSA
 * 
 * @audio - handle do ALSA PCM
 * @out - vetor de saída
 * @len - número de samples a capturar
 * @return void
 */
void Audio::get_audio_raw(snd_pcm_t* audio, std::vector<int16_t>* out, size_t len) {
    int r = snd_pcm_readi(audio, out->data(), len);
    if (r != (int)len) snd_pcm_prepare(audio);
}

/** Realiza o downsampling de um vetor de amostras de áudio.
 *
 * @param input Vetor de entrada com amostras de áudio em formato int16_t.
 * @param output Vetor de saída para as amostras de áudio reduzidas em formato int16_t.
 *
 * A função reduz a taxa de amostragem do áudio de 44100 Hz para 16000 Hz
 * utilizando interpolação linear para evitar aliasing caso DOWNSAMPLE_QUALITY
 * esteja definido. Caso contrário, utiliza o método mais simples de nearest neighbor.
 */
void Audio::downsample(const std::vector<int16_t>& input, std::vector<int16_t>& output) {
    output.resize(SAMPLE_LENGTH);
    size_t input_size = input.size();

#if DOWNSAMPLE_QUALITY
    for (size_t i = 0; i < SAMPLE_LENGTH; ++i) {
        double src_index = i * (input_size - 1.0) / (SAMPLE_LENGTH - 1.0);
        size_t index_floor = static_cast<size_t>(std::floor(src_index));
        size_t index_ceil  = std::min(index_floor + 1, input_size - 1);
        double frac = src_index - index_floor;
        
        double sample = (1.0 - frac) * input[index_floor] + frac * input[index_ceil];
        output[i] = static_cast<int16_t>(sample);
    }
#else    
    for (size_t i = 0; i < SAMPLE_LENGTH; ++i) {
        size_t idx = (i * input_size) / SAMPLE_LENGTH;
        idx = std::min(idx, input_size - 1);
        output[i] = input[idx];
    }
#endif

}

/** Normaliza os samples de áudio
 * 
 * De int16_t para float.
 *
 * @param input Ponteiro para os samples de entrada (int16_t).
 * @param length Quantidade de samples.
 * @param output Vetor de saída já alocado para receber os valores normalizados.
 *
 * De int16_t para float (usando ponteiros).
 *
 * @param input Vetor de entrada com amostras de áudio em formato int16_t.
 * @param output Vetor de saída para as amostras de áudio em formato float.
 *  
 * De int16_t para float (retornando novo vetor).
 * 
 * @param input Vetor de entrada com amostras de áudio em formato int16_t.
 * @return Vetor de saída com amostras normalizadas em formato float.
 */
void Audio::normalize(const int16_t* input, size_t length, std::vector<float>& output) {
    output.resize(length);
    for (size_t i = 0; i < length; i++) {
        output[i] = input[i] * INT16_RECIPROCAL;
    }
}
void Audio::normalize(const std::vector<int16_t>& input, std::vector<float>& output) {
    output.resize(input.size());
    for (size_t i = 0; i < input.size(); i++) {
        output[i] = input[i] * INT16_RECIPROCAL;
    }
}
std::vector<float> Audio::normalize(const std::vector<int16_t>& input) {
    std::vector<float> output;
    normalize(input, output);
    return output;
}

/** Verifica se o sinal de áudio é constante (sem variação).
*  Evita leitura de áudios inválidos (ex: microfone desconectado ou travado).
*
*  @param buffer Buffer contendo os samples de áudio.
*  @param size Tamanho do buffer.
*  @return true se o sinal for constante (todos os valores iguais), false se houver variação.
*/
bool Audio::check_constant_signal(const int16_t* buffer, size_t size) {
    if (!buffer || size == 0) return true; // sinal vazio é considerado constante
    int16_t first = buffer[0];
    for (size_t i = 1; i < size; ++i) {
        if (buffer[i] != first) return false;
    }
    return true;
}
bool Audio::check_constant_signal(const float* buffer, size_t size) {
    if (!buffer || size == 0) return true;
    float first = buffer[0];
    for (size_t i = 1; i < size; ++i) {
        if (buffer[i] != first) return false;
    }
    return true;
}

/** Callback para leitura de dados de áudio do microfone.
*
*   @param offset Posição inicial dos dados a serem lidos.
*   @param length Número de amostras a serem lidas.
*   @param out_ptr Ponteiro para o buffer onde os dados serão armazenados.
*   @return 0 em caso de sucesso, -1 se o offset ou length forem inválidos. 
*/
int Audio::get_signal_audio_data(size_t offset, size_t length, float *out_ptr) {
    if (!out_ptr) return -1;
    if (offset + length > float_samples.size()) {
        length = float_samples.size() - offset; // evita overflow
    }
    memcpy(out_ptr, float_samples.data() + offset, length * sizeof(float));
    return 0;
}

/** [ DEBUG ] Função de debug para escutar o áudio capturado antes ou depois do processamento.
 *
 * @param filename Nome do arquivo WAV a ser salvo.
 * @param samples Amostras de áudio a serem salvas.
 * @param sample_rate Taxa de amostragem das amostras de áudio.
 */
void Audio::save_wav(const std::string &filename, const std::vector<int16_t> &samples, int sample_rate) {
    SF_INFO sfinfo;
    sfinfo.samplerate = sample_rate;
    sfinfo.channels = 1;           // Mono
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    SNDFILE* file = sf_open(filename.c_str(), SFM_WRITE, &sfinfo);
    if (!file) {
#if LOGS_ERRO_AUDIO
        std::cerr << "[ERRO] Erro ao criar arquivo WAV: " << sf_strerror(nullptr) << std::endl;
#endif
        return;
    }

    sf_count_t written = sf_write_short(file, samples.data(), samples.size());
    if (written != (sf_count_t)samples.size()) {
#if LOGS_ERRO_AUDIO
        std::cerr << "[AVISO] Nem todos os samples foram escritos.\n";
#endif
    }

    sf_close(file);
#if LOGS_INFO_AUDIO
    std::cout << "[INFO] Arquivo WAV salvo: " << filename << std::endl;
#endif

}

/** Getter para o vetor de samples em float. 
 * 
 * @return Ponteiro para o vetor de samples correspondente
 */
std::vector<float>* Audio::get_float_samples() {
    return &float_samples;
}

/** Getter para o vetor de samples convertidos (int16_t).
 * 
 * @return Ponteiro para o vetor de samples correspondente
 */
std::vector<int16_t>* Audio::get_converted_samples() {
    return &converted_samples;
}

/** Getter para o vetor de samples brutos (int16_t).
 * 
 * @return Ponteiro para o vetor de samples correspondente
 */
std::vector<int16_t>* Audio::get_raw_samples() {
    return &raw_samples;
}

/** Executa um arquivo de áudio localizado na raiz do projeto.
 * 
 * Utiliza o comando 'aplay' para reproduzir arquivos WAV no dispositivo de saída definido.
 * A execução é bloqueante (aguarda o fim da reprodução).
 *
 * @param filename Nome do arquivo de áudio (ex: "beep.wav", "notification.wav")
 *                 O arquivo deve estar na raiz do projeto ou fornecer caminho relativo
 * @return void
 */
void Audio::run_audio(const std::string &filename) {
    // Construir o caminho completo do arquivo
    std::string filepath = "./" + filename;
    
#if LOGS_INFO_AUDIO
    std::cout << "[INFO] Reproduzindo arquivo de áudio: " << filepath << " no dispositivo: " << PCM_OUTPUT_DEVICE << std::endl;
#endif

    // Usar aplay com o dispositivo de saída especificado
    std::string command = "aplay -D " + std::string(PCM_OUTPUT_DEVICE) + " " + filepath + " 2>/dev/null";
    int result = system(command.c_str());
    
    if (result != 0) {
#if LOGS_ERRO_AUDIO
        std::cerr << "[ERRO] Falha ao reproduzir arquivo: " << filepath << " no dispositivo " << PCM_OUTPUT_DEVICE << " (código: " << result << ")" << std::endl;
#endif
    }
#if LOGS_INFO_AUDIO
    else {
        std::cout << "[INFO] Arquivo reproduzido com sucesso: " << filepath << std::endl;
    }
#endif
}

/**
 * Aplica um filtro passa-alta adaptativo IIR ao sinal de áudio.
 * 
 * Remove ruído de baixa frequência (como vento) de forma suave, preservando
 * a qualidade da fala humana. Pode usar 1ª ou 2ª ordem de filtro.
 * 
 * @param signal - referência ao vetor de amostras float a ser filtrado (IN/OUT)
 * @param cutoff_freq - frequência de corte em Hz (padrão: 80 Hz para preservar fala)
 * @param sample_rate - taxa de amostragem em Hz (padrão: 16000 Hz)
 * @param order - ordem do filtro (1=menos agressivo, 2=mais agressivo)
 * @return void
 */
void Audio::highpass_filter(std::vector<float>& signal, float cutoff_freq, float sample_rate, int order) {
    if (signal.empty() || cutoff_freq <= 0 || sample_rate <= 0) {
        return;
    }
    
    // Normalizar frequência de corte
    float normalized_freq = cutoff_freq / sample_rate;
    if (normalized_freq >= 1.0f) normalized_freq = 0.99f;
    if (normalized_freq <= 0.0f) normalized_freq = 0.01f;
    
    const float PI = 3.14159265358979f;
    float w = 2.0f * PI * normalized_freq;
    
    if (order == 1) {
        // ===== FILTRO PASSA-ALTA DE 1ª ORDEM (MENOS AGRESSIVO) =====
        // Equação: y[n] = a * (y[n-1] + x[n] - x[n-1])
        // onde a = tan(w/2) / (1 + tan(w/2))
        
        float tan_half = std::tan(w / 2.0f);
        float a = tan_half / (1.0f + tan_half);
        
#if LOGS_INFO_AUDIO
        std::cout << "[INFO] Filtro passa-alta 1ª ordem aplicado: fc=" << cutoff_freq 
                  << "Hz, a=" << a << ", fs=" << sample_rate << "Hz\n";
#endif
        
        float y_prev = filter_state_[0];
        float x_prev = last_input_;
        
        for (size_t i = 0; i < signal.size(); i++) {
            float x_n = signal[i];
            // Aplicar filtro: y[n] = a * (y[n-1] + x[n] - x[n-1])
            float y_n = a * (y_prev + x_n - x_prev);
            
            signal[i] = y_n;
            x_prev = x_n;
            y_prev = y_n;
        }
        
        filter_state_[0] = y_prev;
        last_input_ = x_prev;
        
    } else {
        // ===== FILTRO PASSA-ALTA DE 2ª ORDEM (MAIS AGRESSIVO) =====
        // Mantém a implementação anterior
        
        float c = std::tan(w / 2.0f);
        float c2 = c * c;
        
        // Coeficientes de 2ª ordem
        float b0 = 1.0f / (1.0f + c * 1.414213562f + c2);
        float b1 = -2.0f * b0;
        float b2 = b0;
        
        float a1 = 2.0f * (1.0f - c2) * b0;
        float a2 = (1.0f - c * 1.414213562f + c2) * b0;
        
#if LOGS_INFO_AUDIO
        std::cout << "[INFO] Filtro passa-alta 2ª ordem aplicado: fc=" << cutoff_freq 
                  << "Hz, fs=" << sample_rate << "Hz\n";
#endif
        
        float x_n_minus_1 = last_input_;
        float x_n_minus_2 = last_input_2_;
        float y_n_minus_1 = filter_state_[0];
        float y_n_minus_2 = filter_state_[1];
        
        for (size_t i = 0; i < signal.size(); i++) {
            float x_n = signal[i];
            float y_n = b0 * x_n + b1 * x_n_minus_1 + b2 * x_n_minus_2
                        - a1 * y_n_minus_1 - a2 * y_n_minus_2;
            
            signal[i] = y_n;
            x_n_minus_2 = x_n_minus_1;
            x_n_minus_1 = x_n;
            y_n_minus_2 = y_n_minus_1;
            y_n_minus_1 = y_n;
        }
        
        last_input_ = x_n_minus_1;
        last_input_2_ = x_n_minus_2;
        filter_state_[0] = y_n_minus_1;
        filter_state_[1] = y_n_minus_2;
    }
}

/**
 * Aplica suavização adaptativa ao sinal de áudio.
 * Remove cliques, pops e ruído de alta frequência sem afetar a fala.
 * 
 * @param signal - referência ao vetor de amostras float a ser suavizado (IN/OUT)
 * @param threshold - limiar adaptativo para detecção de picos (padrão: 0.02)
 * @return void
 */
void Audio::adaptive_denoise(std::vector<float>& signal, float threshold) {
    if (signal.size() < 3) return;
    
    // Calcular energia média do sinal
    float energy_sum = 0.0f;
    for (float sample : signal) {
        energy_sum += sample * sample;
    }
    float avg_energy = energy_sum / signal.size();
    float threshold_adaptive = threshold * std::sqrt(avg_energy + 1e-6f);
    
    // Aplicar suavização tipo mediana móvel (3-tap)
    std::vector<float> smoothed = signal;
    
    for (size_t i = 1; i < signal.size() - 1; i++) {
        float prev = signal[i - 1];
        float curr = signal[i];
        float next = signal[i + 1];
        
        // Calcular diferença em relação aos vizinhos
        float diff_prev = std::abs(curr - prev);
        float diff_next = std::abs(curr - next);
        
        // Se há grande diferença (spike/click), suavizar
        if (diff_prev > threshold_adaptive || diff_next > threshold_adaptive) {
            // Média ponderada: mais peso para a amostra atual, menos para outliers
            smoothed[i] = 0.5f * curr + 0.25f * prev + 0.25f * next;
        }
    }
    
    signal = smoothed;
}

/**
 * Aplica ganho adaptativo ao sinal para normalizar amplitude.
 * Compensa a atenuação causada pelos filtros passa-alta e suavização.
 * 
 * @param signal - referência ao vetor de amostras float a ser amplificado (IN/OUT)
 * @param target_rms - nível RMS alvo (0.0-1.0, típico 0.3 para preservar dinâmica)
 * @return void
 */
void Audio::adaptive_gain(std::vector<float>& signal, float target_rms) {
    if (signal.empty() || target_rms <= 0.0f) {
        return;
    }
    
    // Calcular RMS do sinal atual
    float sum_squares = 0.0f;
    for (float sample : signal) {
        sum_squares += sample * sample;
    }
    float current_rms = std::sqrt(sum_squares / signal.size());
    
    // Evitar divisão por zero
    if (current_rms < 1e-6f) {
        return;
    }
    
    // Calcular ganho necessário (com limite máximo para evitar clipping)
    float gain = target_rms / current_rms;
    const float MAX_GAIN = 3.0f;  // Limita ganho máximo a 3x para evitar saturação
    
    if (gain > MAX_GAIN) {
        gain = MAX_GAIN;
    }
    
#if LOGS_INFO_AUDIO
    std::cout << "[INFO] Ganho adaptativo aplicado: current_rms=" << current_rms 
              << ", target_rms=" << target_rms << ", gain=" << gain << "\n";
#endif
    
    // Aplicar ganho com proteção contra clipping
    for (float& sample : signal) {
        sample *= gain;
        // Limitar a ±1.0 para evitar clipping
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
    }
}

/**
 * Versão do filtro passa-alta que retorna uma cópia filtrada do sinal.
 * Útil quando não se quer modificar o original.
 * 
 * @param signal - vetor de amostras float (não modificado)
 * @param cutoff_freq - frequência de corte em Hz (padrão: 80 Hz)
 * @param sample_rate - taxa de amostragem em Hz (padrão: 16000 Hz)
 * @param order - ordem do filtro (1=menos agressivo, 2=mais agressivo)
 * @return std::vector<float> - cópia do sinal com filtro passa-alta aplicado
 */
std::vector<float> Audio::highpass_filter_copy(const std::vector<float>& signal, float cutoff_freq, float sample_rate, int order) {
    std::vector<float> filtered_signal = signal;
    highpass_filter(filtered_signal, cutoff_freq, sample_rate, order);
    return filtered_signal;
}