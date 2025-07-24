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

#include "can_ids.h"
#include "can.h"
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/classifier/ei_classifier_types.h"

#define SAMPLE_LENGTH   EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE  // Tamanho do frame de áudio (frame * samples per frame)
#define SAMPLE_RATE     16000                               // Taxa de amostragem do microfone
#define CHANNELS        1                                   // Mono
#define PCM_DEVICE      "default"                           // Dispositivo de áudio ALSA padrão
#define MAX_ATTEMPTS    10                                  // Tentativa para conectar ao microfone
#define DELAY           5000                                // ms de delay entre tentativas

#define ENABLE_CAN      0                                   // Habilita ou desabilita o uso de CAN

static std::vector<float> audio_frame;

/*
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
                (err = snd_pcm_hw_params_set_rate(pcm_handle, params, SAMPLE_RATE, 0)) < 0 ||
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

            std::cout << "[INFO] Áudio configurado para " << SAMPLE_RATE << " Hz, " << CHANNELS << " canal(is), formato S16_LE." << std::endl;
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

/*
*  Verifica se o sinal de áudio é constante (sem variação).
*  Evita leitura de áudios inválidos (ex: microfone desconectado ou travado).
*
*  @param buffer Buffer contendo os samples de áudio.
*  @param size Tamanho do buffer.
*  @return true se o sinal for constante (todos os valores iguais), false se houver variação.
*/
bool isConstantSignal(const int16_t* buffer, size_t size) {
    int16_t first = buffer[0];
    for (size_t i = 1; i < size; ++i) {
        if (buffer[i] != first) return false;
    }
    return true;
}

/*
*   Callback para leitura de dados de áudio do microfone.
*
*   @param offset Posição inicial dos dados a serem lidos.
*   @param length Número de amostras a serem lidas.
*   @param out_ptr Ponteiro para o buffer onde os dados serão armazenados.
*   @return 0 em caso de sucesso, -1 se o offset ou length forem inválidos. 
*/
int get_signal_audio_data(size_t offset, size_t length, float *out_ptr) {
    if (offset + length > audio_frame.size()) return -1;
    memcpy(out_ptr, audio_frame.data() + offset, length * sizeof(float));
    return 0;
}

/*
*   Captura um frame de áudio do microfone e preenche a estrutura signal_t.
*
*   @param signal Ponteiro para a estrutura signal_t a ser preenchida.
*   @param pcm_handle Ponteiro para o dispositivo de áudio PCM.
*   @return true se o frame foi capturado com sucesso, false em caso de erro.
*/
bool get_audio_frame_signal(signal_t *signal, snd_pcm_t *pcm_handle) {
    short buffer[SAMPLE_LENGTH];
    int err = snd_pcm_readi(pcm_handle, buffer, SAMPLE_LENGTH);
    if (err != SAMPLE_LENGTH) {
        std::cerr << "[ERRO] Falha na leitura do microfone: " << snd_strerror(err) << std::endl;
        return false;
    }

    audio_frame.resize(SAMPLE_LENGTH);
    for (size_t i = 0; i < SAMPLE_LENGTH; i++) {
        audio_frame[i] = buffer[i] / 32768.0f;
    }

    signal->total_length = audio_frame.size();
    signal->get_data = &get_signal_audio_data;
    return true;
}

/*
*   Detecta a palavra-chave "Zenira" no áudio capturado.
*
*   @param audio_samples Vetor contendo os samples de áudio capturados.
*   @param signal Ponteiro para a estrutura signal_t que será preenchida.
*   @param pcm_handle Ponteiro para o dispositivo de áudio PCM.
*   @return true se a palavra-chave foi detectada, false caso contrário ou em caso de erro.
*/
bool wake_word_detected(std::vector<float>& audio_samples, signal_t* signal, snd_pcm_t* pcm_handle) {
    if (!get_audio_frame_signal(signal, pcm_handle)) {
        std::cerr << "[ERRO] Falha ao capturar frame de áudio.\n";
        return false;
    }

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

/*
*   Cria um reconhecedor de comandos Vosk com um modelo pré-carregado.
*
*   @param model Ponteiro para o modelo Vosk carregado.
*   @return Ponteiro para o reconhecedor de comandos ou nullptr em caso de erro.
*/
VoskRecognizer* criarCommandRecognizer(VoskModel* model) {
    const char* grammar = R"([
        "desligar motor", "ligar motor",
        "mudar velocidade para dez",
        "mudar velocidade para vinte",
        "mudar velocidade para trinta",
        "mudar velocidade para quarenta",
        "mudar velocidade para cinquenta",
        "mudar velocidade para sessenta",
        "mudar velocidade para setenta",
        "mudar velocidade para oitenta",
        "mudar velocidade para noventa",
        "mudar velocidade para cem",
        "virar a direita",
        "virar a esquerda",
        "seguir reto"
    ])";

    return vosk_recognizer_new_grm(model, SAMPLE_RATE, grammar);
}

/*
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

/*
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

int main() {
    setlogmask(LOG_UPTO(LOG_ERR));
    std::cout << "[INFO] Carregando modelo Vosk...\n";

    VoskModel* model = vosk_model_new("vosk-models/vosk-model-small-pt-0.3");
    if (!model) {
        std::cerr << "[ERRO] Falha ao carregar modelo Vosk.\n";
        return 1;
    }

    signal_t signal;
    snd_pcm_t* audio = init_audio();
    if (!audio) return 1;

    std::vector<float> float_samples(SAMPLE_LENGTH);
    short raw_samples[SAMPLE_LENGTH];

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
        int err = snd_pcm_readi(audio, raw_samples, SAMPLE_LENGTH);
        if (err != SAMPLE_LENGTH) {
            std::cerr << "[ERRO] Falha ao ler dados de áudio: " << snd_strerror(err) << std::endl;
            continue;
        }

        if (isConstantSignal(raw_samples, SAMPLE_LENGTH)) {
            std::cerr << "[INFO] Sinal de áudio constante detectado. Ignorando frame.\n";
            continue;
        }

        for (int i = 0; i < SAMPLE_LENGTH; i++) {
            float_samples[i] = raw_samples[i] / 32768.0f;
        }


        if (wake_word_detected(float_samples, &signal, audio)) {
            std::cout << "[INFO] Iniciando reconhecimento de comandos com Vosk...\n";
            VoskRecognizer* recognizer = criarCommandRecognizer(model);

            bool comandoReconhecido = false;
            time_t inicio = time(nullptr);
            while (difftime(time(nullptr), inicio) < 5.0) {
                snd_pcm_readi(audio, raw_samples, SAMPLE_LENGTH / 2);
                if (vosk_recognizer_accept_waveform(recognizer, (const char*)raw_samples, (SAMPLE_LENGTH / 2) * sizeof(short))) {
                    std::string result = vosk_recognizer_result(recognizer);
                    Json::Reader reader;
                    Json::Value root;
                    if (reader.parse(result, root)) {
                        std::string comando = root["text"].asString();
                        std::cout << "[COMANDO] Detectado: \"" << comando << "\"\n";

                        uint8_t dados[] = {0x01};

                        if (comando == "desligar motor") {
                            std::cout << "[INFO] Desligando motor.\n";
                            send_command_motor(can_sock, 0);
                        }
                        else if (comando == "ligar motor") {
                            std::cout << "[INFO] Ligando motor.\n";
                            send_command_motor(can_sock, 5); 
                        }
                        else if (comando == "mudar velocidade para 10" || comando == "mudar velocidade para dez") {
                            std::cout << "[INFO] Ajustando velocidade do motor para 10%.\n";
                            send_command_motor(can_sock, 10);
                        }
                        else if (comando == "mudar velocidade para 20" || comando == "mudar velocidade para vinte") {
                            std::cout << "[INFO] Ajustando velocidade do motor para 20%.\n";
                            send_command_motor(can_sock, 20);
                        }
                        else if (comando == "mudar velocidade para 30" || comando == "mudar velocidade para trinta") {
                            std::cout << "[INFO] Ajustando velocidade do motor para 30%.\n";
                            send_command_motor(can_sock, 30);
                        }
                        else if (comando == "mudar velocidade para 40" || comando == "mudar velocidade para quarenta") {
                            std::cout << "[INFO] Ajustando velocidade do motor para 40%.\n";
                            send_command_motor(can_sock, 40);
                        }
                        else if (comando == "mudar velocidade para 50" || comando == "mudar velocidade para cinquenta") {
                            std::cout << "[INFO] Ajustando velocidade do motor para 50%.\n";
                            send_command_motor(can_sock, 50);
                        }
                        else if (comando == "mudar velocidade para 60" || comando == "mudar velocidade para sessenta") {
                            std::cout << "[INFO] Ajustando velocidade do motor para 60%.\n";
                            send_command_motor(can_sock, 60);
                        }
                        else if (comando == "mudar velocidade para 70" || comando == "mudar velocidade para setenta") {
                            std::cout << "[INFO] Ajustando velocidade do motor para 70%.\n";
                            send_command_motor(can_sock, 70);
                        }
                        else if (comando == "mudar velocidade para 80" || comando == "mudar velocidade para oitenta") {
                            std::cout << "[INFO] Ajustando velocidade do motor para 80%.\n";
                            send_command_motor(can_sock, 80);
                        }
                        else if (comando == "mudar velocidade para 90" || comando == "mudar velocidade para noventa") {
                            std::cout << "[INFO] Ajustando velocidade do motor para 90%.\n";
                            send_command_motor(can_sock, 90);
                        }
                        else if (comando == "mudar velocidade para 100" || comando == "mudar velocidade para cem") {
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
