/*
* File:  main_full.cpp
* Autor: Victor Lompa Schwider
*
* Este arquivo integra a implementação do reconhecimento da wake-word "Zenira"
* utilizando o Edge Impulse SDK e a biblioteca Vosk para reconhecimento de fala.
* os comandos reconhecidos são enviados via protocolo CAN e se comunica com diversos
* outros módulos do sistema do barco movido a energia solar da equipe Zênite Solar.
*/

/* Includes */
// Debug includes para salvar arquivos WAV
#include "/usr/include/sndfile.h"
#include <iomanip>
#include <sstream>

#include <cstdint>
#include <cmath>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <jsoncpp/json/json.h>
#include <syslog.h>
#include <vosk_api.h>
#include <alsa/asoundlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <cerrno>
#include <sys/ioctl.h>

/* Local includes */
#include "can_ids.h"
#include "can.h"
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/classifier/ei_classifier_types.h"

/* Defines */

// Alsa configs
#define SAMPLE_LENGTH       EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE  // Tamanho do frame de áudio (frame * samples per frame)
#define PCM_DEVICE          "plughw:1,0"                        // Dispositivo de áudio ALSA padrão
#define CHANNELS            1                                   // Canais de áudio (mono)
#define ALSA_SAMPLE_RATE    44100                               // Taxa de amostragem do microfone (ALSA)
#define TARGET_SAMPLE_RATE  16000                               // Taxa de amostragem alvo (Vosk e Edge Impulse)

// Reconnect configs
#define MAX_ATTEMPTS        10                                  // Tentativa para conectar ao microfone
#define DELAY               5000                                // ms de delay entre tentativas

// Debug configs
#define VOSK_LOG_LEVEL      1                                   // Nível de log do Vosk (0: desativado, 1: erros, 2: avisos)
#define ENABLE_CAN          1                                   // Habilita ou desabilita o uso de CAN
#define SAVE_TEST_WAV_RAW   0                                   // Salva arquivos WAV para debug antes do processamento
#define SAVE_TEST_WAV_DS    0                                   // Salva arquivos WAV para debug depois do downsample

/* Variables */
std::vector<float> float_samples(SAMPLE_LENGTH);
std::vector<int16_t> converted_samples(SAMPLE_LENGTH);
std::vector<int16_t> raw_samples(ALSA_SAMPLE_RATE);

/* Debug Varibles */
int wav_counter = 0;

/**
 * [ DEBUG ]
 * Função de debug para escutar o áudio capturado antes ou depois do processamento.
 *
 * @param filename Nome do arquivo WAV a ser salvo.
 * @param samples Amostras de áudio a serem salvas.
 * @param sample_rate Taxa de amostragem das amostras de áudio.
 */
void save_wav(const std::string &filename, const std::vector<int16_t> &samples, int sample_rate) {
    SF_INFO sfinfo;
    sfinfo.samplerate = sample_rate;
    sfinfo.channels = 1;           // Mono
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    SNDFILE* file = sf_open(filename.c_str(), SFM_WRITE, &sfinfo);
    if (!file) {
        std::cerr << "Erro ao criar arquivo WAV: " << sf_strerror(nullptr) << std::endl;
        return;
    }

    sf_count_t written = sf_write_short(file, samples.data(), samples.size());
    if (written != (sf_count_t)samples.size()) {
        std::cerr << "Aviso: nem todos os samples foram escritos.\n";
    }

    sf_close(file);
    std::cout << "[INFO] Arquivo WAV salvo: " << filename << std::endl;
}

/**
*   Inicializa o dispositivo de áudio ALSA e configura os parâmetros necessários.
*
*   @return snd_pcm_t* Ponteiro para o dispositivo de áudio ALSA ou nullptr em caso de erro.
*/
snd_pcm_t* init_audio() {
    snd_pcm_t *pcm_handle = nullptr;
    snd_pcm_hw_params_t *params = nullptr;
    int err;

    std::cout << "[INFO] Inicializando captura de áudio ALSA em \"" << PCM_DEVICE << "\"..." << std::endl;

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
        err = snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_CAPTURE, 0);
        if (err >= 0) {
            std::cout << "[INFO] Microfone aberto na tentativa " << attempt << " de " << MAX_ATTEMPTS << "." << std::endl;
            
            if ((err = snd_pcm_hw_params_malloc(&params)) < 0) {
                std::cerr << "[ERRO] Falha ao alocar hw_params: " << snd_strerror(err) << std::endl;
                snd_pcm_close(pcm_handle);
                return nullptr;
            }

            if ((err = snd_pcm_hw_params_any(pcm_handle, params)) < 0 ||
                (err = snd_pcm_hw_params_set_access(pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
                (err = snd_pcm_hw_params_set_format(pcm_handle, params, SND_PCM_FORMAT_S16_LE)) < 0 ||
                (err = snd_pcm_hw_params_set_channels(pcm_handle, params, CHANNELS)) < 0 ||
                (err = snd_pcm_hw_params_set_rate(pcm_handle, params, ALSA_SAMPLE_RATE, 0)) < 0 ||
                (err = snd_pcm_hw_params(pcm_handle, params)) < 0) {

                std::cerr << "[ERRO] Falha ao configurar hw_params: " << snd_strerror(err) << std::endl;
                snd_pcm_hw_params_free(params);
                snd_pcm_close(pcm_handle);
                return nullptr;
            }

            snd_pcm_hw_params_free(params);

            if ((err = snd_pcm_prepare(pcm_handle)) < 0) {
                std::cerr << "[ERRO] Falha ao preparar PCM: " << snd_strerror(err) << std::endl;
                snd_pcm_close(pcm_handle);
                return nullptr;
            }

            std::cout << "[INFO] Áudio configurado para " << ALSA_SAMPLE_RATE << " Hz, " << CHANNELS << " canal(is), formato S16_LE." << std::endl;
            return pcm_handle;
        }

        std::cerr << "[WARN] Tentativa " << attempt << " falhou: " << snd_strerror(err) << std::endl;

        if (attempt < MAX_ATTEMPTS) {
            std::cerr << "[INFO] Aguardando " << DELAY << "ms antes de tentar novamente...\n";
            usleep(DELAY * 1000);
        }
    }

    std::cerr << "[ERRO] Não foi possível inicializar o microfone após " << MAX_ATTEMPTS << " tentativas.\n";
    return nullptr;
}

/**
*   Callback para leitura de dados de áudio do microfone.
*
*   @param offset Posição inicial dos dados a serem lidos.
*   @param length Número de amostras a serem lidas.
*   @param out_ptr Ponteiro para o buffer onde os dados serão armazenados.
*   @return 0 em caso de sucesso, -1 se o offset ou length forem inválidos. 
*/
int get_signal_audio_data(size_t offset, size_t length, float *out_ptr) {
    if (!out_ptr) return -1;
    if (offset + length > float_samples.size()) {
        length = float_samples.size() - offset; // evita overflow
    }
    memcpy(out_ptr, float_samples.data() + offset, length * sizeof(float));
    return 0;
}

/**
*  Verifica se o sinal de áudio é constante (sem variação).
*  Evita leitura de áudios inválidos (ex: microfone desconectado ou travado).
*
*  @param buffer Buffer contendo os samples de áudio.
*  @param size Tamanho do buffer.
*  @return true se o sinal for constante (todos os valores iguais), false se houver variação.
*/
bool check_constant_signal(const int16_t* buffer, size_t size) {
    if (!buffer || size == 0) return true; // sinal vazio é considerado constante
    int16_t first = buffer[0];
    for (size_t i = 1; i < size; ++i) {
        if (buffer[i] != first) return false;
    }
    return true;
}
bool check_constant_signal(const float* buffer, size_t size) {
    if (!buffer || size == 0) return true;
    float first = buffer[0];
    for (size_t i = 1; i < size; ++i) {
        if (buffer[i] != first) return false;
    }
    return true;
}

/**
 * Realiza o downsampling de um vetor de amostras de áudio.
 *
 * @param input Vetor de entrada com amostras de áudio em formato int16_t.
 * @param output Vetor de saída para as amostras de áudio reduzidas em formato int16_t.
 *
 * A função reduz a taxa de amostragem do áudio de 44100 Hz para 16000 Hz
 * utilizando interpolação linear para evitar aliasing.
 */
void downsample(const std::vector<int16_t>& input, std::vector<int16_t>& output) {
    output.resize(SAMPLE_LENGTH); // tamanho fixo

    size_t input_size = input.size();
    for (size_t i = 0; i < SAMPLE_LENGTH; ++i) {
        double src_index = i * (input_size - 1.0) / (SAMPLE_LENGTH - 1.0);
        size_t index_floor = static_cast<size_t>(std::floor(src_index));
        size_t index_ceil  = std::min(index_floor + 1, input_size - 1);
        double frac = src_index - index_floor;

        double sample = (1.0 - frac) * input[index_floor] + frac * input[index_ceil];
        output[i] = static_cast<int16_t>(sample);
    }
}

/**
 * Normaliza os samples de áudio de int16_t para float.
 *
 * @param input Ponteiro para os samples de entrada (int16_t).
 * @param length Quantidade de samples.
 * @param output Vetor de saída já alocado para receber os valores normalizados.
 *
 * Normaliza os samples de áudio de int16_t para float (usando ponteiros).
 *
 * @param input Vetor de entrada com amostras de áudio em formato int16_t.
 * @param output Vetor de saída para as amostras de áudio em formato float.
 *  
 * Normaliza os samples de áudio de int16_t para float (retornando novo vetor).
 * 
 * @param input Vetor de entrada com amostras de áudio em formato int16_t.
 * @return Vetor de saída com amostras normalizadas em formato float.
 */
void normalize(const int16_t* input, size_t length, std::vector<float>& output) {
    output.resize(length);
    for (size_t i = 0; i < length; i++) {
        output[i] = input[i] / 32768.0f;
    }
}
void normalize(const std::vector<int16_t>& input, std::vector<float>& output) {
    output.resize(input.size());
    for (size_t i = 0; i < input.size(); i++) {
        output[i] = input[i] / 32768.0f;
    }
}
std::vector<float> normalize(const std::vector<int16_t>& input) {
    std::vector<float> output;
    normalize(input, output);
    return output;
}

/**
*   Detecta a palavra-chave "Zenira" no áudio capturado.
*
*   @param signal Ponteiro para a estrutura signal_t que será preenchida.
*   @return true se a palavra-chave foi detectada, false caso contrário ou em caso de erro.
*/
bool wake_word_detected(signal_t* signal) {
    ei_impulse_result_t result;
    EI_IMPULSE_ERROR res = run_classifier(signal, &result, false);

    if (res != EI_IMPULSE_OK) {
        std::cerr << "Erro ao classificar: " << res << std::endl;
        return false;
    }

    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        std::string label(result.classification[ix].label);
        float value = result.classification[ix].value;
        std::cout << label << ": " << value << std::endl;

        if (label == "Zenira" && value > 0.8f) {
            std::cout << "[Wake word detectada!]" << std::endl;
            return true;
        }
    }

    return false;
}

/**
*   Cria um reconhecedor de comandos Vosk com um modelo pré-carregado.
*
*   @param model Ponteiro para o modelo Vosk carregado.
*   @return Ponteiro para o reconhecedor de comandos ou nullptr em caso de erro.
*/
VoskRecognizer* create_command_recognizer(VoskModel* model) {
    const char* grammar = R"([
        "mudar velocidade para dez",       "velocidade para dez",       "velocidade dez",
        "mudar velocidade para vinte",     "velocidade para vinte",     "velocidade vinte",
        "mudar velocidade para trinta",    "velocidade para trinta",    "velocidade trinta",
        "mudar velocidade para quarenta",  "velocidade para quarenta",  "velocidade quarenta",
        "mudar velocidade para cinquenta", "velocidade para cinquenta", "velocidade cinquenta",
        "mudar velocidade para sessenta",  "velocidade para sessenta",  "velocidade sessenta",
        "mudar velocidade para setenta",   "velocidade para setenta",   "velocidade setenta",
        "mudar velocidade para oitenta",   "velocidade para oitenta",   "velocidade oitenta",
        "mudar velocidade para noventa",   "velocidade para noventa",   "velocidade noventa",
        "mudar velocidade para cem",       "velocidade para cem",       "velocidade cem",
        "desligar motor", "motor off",
        "ligar motor", "motor on",
        "virar a direita", "virar a esquerda", "seguir reto"
    ])";

    return vosk_recognizer_new_grm(model, ALSA_SAMPLE_RATE, grammar);
}

/**
*   Envia um comando para o MIC via CAN.
*
*   @param duty_cycle Valor do duty cycle para o motor (0-100).
*   @param can_sock Socket CAN já configurado.
*/
void send_command_motor(int can_sock, uint8_t duty_cycle) {
#if ENABLE_CAN
    uint8_t dados[CAN_MSG_MCV25_MOTOR_LENGTH];

    // Preenchimento da estrutura da mensagem:
    dados[CAN_MSG_MCV25_MOTOR_SIGNATURE_BYTE] = CAN_SIGNATURE_MCV25;

    // MOTOR_STATE bits: MOTOR_ON (bit 0), DMS_ON (bit 1), REVERSE (bit 2)
    dados[CAN_MSG_MCV25_MOTOR_MOTOR_BYTE] = 
        (1 << CAN_MSG_MCV25_MOTOR_MOTOR_MOTOR_ON_BIT) |
        (1 << CAN_MSG_MCV25_MOTOR_MOTOR_DMS_ON_BIT); // Não setamos REVERSE

    dados[CAN_MSG_MCV25_MOTOR_D_BYTE] = duty_cycle;  // Duty Cycle (%)
    dados[CAN_MSG_MCV25_MOTOR_I_BYTE] = 100;         // Soft start (%)

    if (send_can(can_sock, CAN_MSG_MCV25_MOTOR_ID, dados, CAN_MSG_MCV25_MOTOR_LENGTH)) {
        std::cout << "[Info] Velocidade ajustada para " << static_cast<int>(duty_cycle) << "%\n";
    } else {
        std::cerr << "[ERRO] Falha ao enviar comando de motor\n";
    }
#else
    std::cout << "[INFO] CAN desativado. Comando de motor não enviado.\n";
#endif
}

/**
*   Envia um comando para o MDE via CAN.
*
*   @param posicao_graus_cem Posição da rabeta em centésimos de grau (±45.00 graus).
*   @param can_sock Socket CAN já configurado.
*/
void send_command_tail(int can_sock, int16_t posicao_graus_cem) {
#if ENABLE_CAN
    uint8_t dados[CAN_MSG_MCV25_MDE_LENGTH];

    // Garantia de limites físicos (±45.00 graus)
    if (posicao_graus_cem < -4500) posicao_graus_cem = -4500;
    if (posicao_graus_cem >  4500) posicao_graus_cem =  4500;

    // Preenche assinatura
    dados[CAN_MSG_MCV25_MDE_SIGNATURE_BYTE] = CAN_SIGNATURE_MCV25;

    // Conversão para little-endian
    uint16_t pos_unsigned = static_cast<uint16_t>(posicao_graus_cem);
    dados[CAN_MSG_MCV25_MDE_POSITION_L_BYTE] = pos_unsigned & 0xFF;
    dados[CAN_MSG_MCV25_MDE_POSITION_H_BYTE] = (pos_unsigned >> 8) & 0xFF;

    if (send_can(can_sock, CAN_MSG_MCV25_MDE_ID, dados, CAN_MSG_MCV25_MDE_LENGTH)) {
        std::cout << "[INFO] Direção da rabeta: " << (posicao_graus_cem / 100.0f) << "°\n";
    } else {
        std::cerr << "[ERRO] Falha ao enviar comando de rabeta\n";
    }
#else
    std::cout << "[INFO] CAN desativado. Comando de rabeta não enviado.\n";
#endif
}

/**
 * Obtém o áudio do dispositivo de captura.
 *
 * @param audio Dispositivo de captura de áudio.
 * @param raw_samples Buffer para armazenar os samples de áudio brutos.
 * @param sample_length Número de samples a serem lidos (padrão: SAMPLE_LENGTH).
 * @param op Operação a ser realizada: 'd' (downsample), 'n' (normalize), 'b' (both).
 *
 * @return Estrutura de sinal contendo os dados de áudio.
 */
signal_t get_audio(snd_pcm_t* audio, std::vector<int16_t>* raw_samples_ptr) {
    if (!raw_samples_ptr) return {};

    size_t offset = 0;
    std::vector<int16_t>& raw_samples = *raw_samples_ptr;

    // Preenche 1 segundo de áudio bruto
    while (offset < raw_samples.size()) {
        int frames_to_read = raw_samples.size() - offset;
        int err = snd_pcm_readi(audio, raw_samples.data() + offset, frames_to_read);
        if (err < 0) {
            std::cerr << "[ERRO] Falha ao ler dados de áudio: " << snd_strerror(err) << std::endl;
            snd_pcm_prepare(audio);
            return {}; // sinal inválido
        }
        offset += err;
    }

    // Verifica sinal constante
    if (check_constant_signal(raw_samples.data(), raw_samples.size())) {
        std::cerr << "[INFO] Sinal de áudio constante detectado. Ignorando frame.\n";
        return {};
    }

#if SAVE_TEST_WAV_RAW
    std::stringstream ss;
    ss << "teste_audio_raw_" << std::setw(3) << std::setfill('0') << wav_counter++ << ".wav";
    save_wav(ss.str(), raw_samples, ALSA_SAMPLE_RATE);
#endif

    // Downsample e normalização conforme opção selecionada
    if(ALSA_SAMPLE_RATE <= TARGET_SAMPLE_RATE) {
        std::cerr << "[WARN] Frequência de entrada é menor ou igual a frequência alvo. Pulando downsample.\n";
        converted_samples = raw_samples;
    } else {
        downsample(raw_samples, converted_samples);
#if SAVE_TEST_WAV_DS
        std::stringstream ss;
        ss << "teste_audio_ds_" << std::setw(3) << std::setfill('0') << wav_counter++ << ".wav";
        save_wav(ss.str(), converted_samples, TARGET_SAMPLE_RATE);
#endif
    }

    normalize(converted_samples, float_samples);

    // Preenche a estrutura de sinal
    signal_t signal;
    signal.total_length = float_samples.size();
    signal.get_data = &get_signal_audio_data;
    return signal;
}

/**
 * Lê dados brutos de áudio do dispositivo de captura.
 *
 * @param audio Dispositivo de captura de áudio.
 * @param raw_samples Buffer para armazenar os samples de áudio brutos.
 * @param sample_length Número de samples a serem lidos (padrão: SAMPLE_LENGTH).
 */
void get_raw_audio(snd_pcm_t* audio, std::vector<int16_t> *raw_samples, size_t sample_length = SAMPLE_LENGTH) {
    int err = snd_pcm_readi(audio, raw_samples->data(), sample_length);

    // Verifica se houve erro na leitura
    if (err != sample_length) {
        std::cerr << "[ERRO] Falha ao ler dados de áudio: " << snd_strerror(err) << std::endl;
        snd_pcm_prepare(audio);
        return;
    }

    // Verifica se o sinal de áudio é constante
    if (check_constant_signal(raw_samples->data(), sample_length)) {
        std::cerr << "[INFO] Sinal de áudio constante detectado. Ignorando frame.\n";
        return;
    }
}

/**
 * Verifica e executa comandos recebidos.
 * 
 * @param comando Comando reconhecido em formato string.
 * @param can_sock Socket CAN já configurado.
 */
bool execute_commands(const std::string& comando, int can_sock) {
    if (comando == "desligar motor") {
        std::cout << "[INFO] Desligando motor.\n";
        send_command_motor(can_sock, 0);
    }
    else if (comando == "ligar motor") {
        std::cout << "[INFO] Ligando motor.\n";
        send_command_motor(can_sock, 5); 
    }
    else if (comando == "mudar velocidade para dez" || 
                comando == "velocidade para dez" || 
                comando == "velocidade dez") {
        std::cout << "[INFO] Ajustando velocidade do motor para 10%.\n";
        send_command_motor(can_sock, 10);
    }
    else if (comando == "mudar velocidade para vinte" || 
                comando == "velocidade para vinte" || 
                comando == "velocidade vinte") {
        std::cout << "[INFO] Ajustando velocidade do motor para 20%.\n";
        send_command_motor(can_sock, 20);
    }
    else if (comando == "mudar velocidade para trinta" || 
                comando == "velocidade para trinta" || 
                comando == "velocidade trinta") {
        std::cout << "[INFO] Ajustando velocidade do motor para 30%.\n";
        send_command_motor(can_sock, 30);
    }
    else if (comando == "mudar velocidade para quarenta" || 
                comando == "velocidade para quarenta" || 
                comando == "velocidade quarenta") {
        std::cout << "[INFO] Ajustando velocidade do motor para 40%.\n";
        send_command_motor(can_sock, 40);
    }
    else if (comando == "mudar velocidade para cinquenta" || 
                comando == "velocidade para cinquenta" || 
                comando == "velocidade cinquenta") {
        std::cout << "[INFO] Ajustando velocidade do motor para 50%.\n";
        send_command_motor(can_sock, 50);
    }
    else if (comando == "mudar velocidade para sessenta" || 
                comando == "velocidade para sessenta" || 
                comando == "velocidade sessenta") {
        std::cout << "[INFO] Ajustando velocidade do motor para 60%.\n";
        send_command_motor(can_sock, 60);
    }
    else if (comando == "mudar velocidade para setenta" || 
                comando == "velocidade para setenta" || 
                comando == "velocidade setenta") {
        std::cout << "[INFO] Ajustando velocidade do motor para 70%.\n";
        send_command_motor(can_sock, 70);
    }
    else if (comando == "mudar velocidade para oitenta" || 
                comando == "velocidade para oitenta" || 
                comando == "velocidade oitenta") {
        std::cout << "[INFO] Ajustando velocidade do motor para 80%.\n";
        send_command_motor(can_sock, 80);
    }
    else if (comando == "mudar velocidade para noventa" || 
                comando == "velocidade para noventa" || 
                comando == "velocidade noventa") {
        std::cout << "[INFO] Ajustando velocidade do motor para 90%.\n";
        send_command_motor(can_sock, 90);
    }
    else if (comando == "mudar velocidade para cem" || 
                comando == "velocidade para cem" || 
                comando == "velocidade cem") {
        std::cout << "[INFO] Ajustando velocidade do motor para 100%.\n";
        send_command_motor(can_sock, 100);
    }
    else if (comando == "virar a direita") {
        std::cout << "[INFO] Virando a rabeta para a direita.\n";
        send_command_tail(can_sock, +3000);
    }
    else if (comando == "virar a esquerda") {
        std::cout << "[INFO] Virando a rabeta para a esquerda.\n";
        send_command_tail(can_sock, -3000);
    }
    else if (comando == "seguir reto") {
        std::cout << "[INFO] Ajustando rabeta para posição zero.\n";
        send_command_tail(can_sock, 0);
    }
    else if (!comando.empty()) {
        std::cout << "[INFO] Comando não reconhecido: " << comando << "\n";
    }

    return true;
}

/**
 * Função principal do programa.
 */
int main() {
    setlogmask(LOG_UPTO(LOG_ERR));
    std::cout << "[INFO] Carregando modelo Vosk...\n";

    vosk_set_log_level(VOSK_LOG_LEVEL);
    VoskModel* model = vosk_model_new("vosk-models/vosk-model-small-pt-0.3");
    if (!model) {
        std::cerr << "[ERRO] Falha ao carregar modelo Vosk.\n";
        return 1;
    }

    signal_t signal;
    snd_pcm_t* audio = init_audio();

    if (!audio) {
        std::cerr << "[ERRO] PCM não inicializado!\n";
        return 1;
    }

#if ENABLE_CAN
    int can_sock = setup_can();
    if (can_sock < 0) {
        std::cerr << "[ERRO] Falha ao configurar interface CAN.\n";
        return 1;
    }
#else
    int can_sock = -1;
    std::cout << "[INFO] CAN desativado para testes locais.\n";
#endif

    std::cout << "[INFO] Aguardando palavra de ativação: \"zenira\"...\n";

    while (true) {
        signal = get_audio(audio, &raw_samples);  
        
        if (signal.total_length == 0 || signal.get_data == nullptr) {
            continue; // ou break para não tentar classificar
        }

        if (wake_word_detected(&signal)) {
            std::cout << "[INFO] Iniciando reconhecimento de comandos com Vosk...\n";
            VoskRecognizer* recognizer = create_command_recognizer(model);
            bool comandoReconhecido = false;
            time_t inicio = time(nullptr);
            while (difftime(time(nullptr), inicio) < 5.0) {
                get_raw_audio(audio, &raw_samples, SAMPLE_LENGTH / 2);
                if (vosk_recognizer_accept_waveform(recognizer, (const char*)raw_samples.data(), (SAMPLE_LENGTH / 2) * sizeof(int16_t))) {
                    std::string  result = vosk_recognizer_result(recognizer);
                    Json::Reader reader;
                    Json::Value  root;
                    if (reader.parse(result, root)) {
                        std::string comando = root["text"].asString();
                        std::cout << "[COMANDO] Detectado: \"" << comando << "\"\n";
#if ENABLE_CAN
                        execute_commands(comando, can_sock);
#endif
                        comandoReconhecido = true;
                        break;
                    }
                }
            }

            vosk_recognizer_free(recognizer);

            if (!comandoReconhecido) std::cout << "[INFO] Nenhum comando detectado dentro do tempo limite.\n";
            std::cout << "[INFO] Retornando ao modo de escuta da palavra-chave \"zenira\"...\n";
        }

        usleep(10000);
    }

    vosk_model_free(model);
    snd_pcm_close(audio);

#if ENABLE_CAN
    close_can(can_sock);
#endif

    return 0;
}
