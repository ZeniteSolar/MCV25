/*
* File:  main.cpp
* Autor: Victor Lompa Schwider
*
* Este arquivo integra a implementação do reconhecimento da wake-word "Zenira"
* utilizando o Edge Impulse SDK e a biblioteca Vosk para reconhecimento de fala.
* os comandos reconhecidos são enviados via protocolo CAN e se comunica com diversos
* outros módulos do sistema do barco movido a energia solar da equipe Zênite Solar.
*/

/* Standard includes */
#include <iostream>
#include <string>

/* Local includes */
#include "vosk.h"
#include "can.h"
#include "audio.h"
#include "edge_impulse.h"

/* Defines */
#define LOGS_ERRO           0                                   // Habilita ou desabilita logs de erro e de aviso
#define LOGS_INFO           0                                   // Habilita ou desabilita logs informativos
#define VOSK_LOG_LEVEL      0                                   // Nível de log do Vosk (0: desativado, 1: erros, 2: avisos)
#define ENABLE_CAN          1                                   // Habilita ou desabilita o uso de CAN

/**
 * Função principal do programa.
 */
int main() {

#if LOGS_INFO
    std::cout << "[INFO] Carregando modelo Vosk...\n";
#endif

    vosk_set_log_level(VOSK_LOG_LEVEL);
    VoskModel* model = vosk_model_new("vosk-models/vosk-model-small-pt-0.3");
    if (!model) {
#if LOGS_ERRO
        std::cerr << "[ERRO] Falha ao carregar modelo Vosk.\n";
#endif
        return 1;
    }

    signal_t signal;
    snd_pcm_t* audio = audio_init();

    if (!audio) {
#if LOGS_ERRO
        std::cerr << "[ERRO] PCM não inicializado!\n";
#endif
        return 1;
    }

#if ENABLE_CAN
    int can_sock = setup_can();
    if (can_sock < 0) {
#if LOGS_ERRO
        std::cerr << "[ERRO] Falha ao configurar interface CAN.\n";
#endif
        return 1;
    }
#else
    int can_sock = -1;
#if LOGS_INFO
    std::cout << "[INFO] CAN desativado para testes locais.\n";
#endif
#endif

#if LOGS_INFO
    std::cout << "[INFO] Aguardando palavra de ativação: \"zenira\"...\n";
#endif

    while (true) {
        signal = get_audio(audio, &raw_samples);  
        
        if (signal.total_length == 0 || signal.get_data == nullptr) {
            continue; // ou break para não tentar classificar
        }

        if (wake_word_detected(&signal)) {
#if LOGS_ERRO
            std::cout << "[INFO] Iniciando reconhecimento de comandos com Vosk...\n";
#endif
            VoskRecognizer* recognizer = create_command_recognizer(model);
            bool comandoReconhecido = false;
            time_t inicio = time(nullptr);
            while (difftime(time(nullptr), inicio) < 5.0) {
                get_audio_raw(audio, &raw_samples, SAMPLE_LENGTH / 2);
                if (vosk_recognizer_accept_waveform(recognizer, (const char*)raw_samples.data(), (SAMPLE_LENGTH / 2) * sizeof(int16_t))) {
                    std::string  result = vosk_recognizer_result(recognizer);
                    Json::Reader reader;
                    Json::Value  root;
                    if (reader.parse(result, root)) {
                        std::string comando = root["text"].asString();
#if LOGS_INFO
                        std::cout << "[INFO] Detectado: \"" << comando << "\"\n";
#endif

#if ENABLE_CAN
                        execute_commands(comando, can_sock);
#endif
                        comandoReconhecido = true;
                        break;
                    }
                }
            }

            vosk_recognizer_free(recognizer);

#if LOGS_INFO
            if (!comandoReconhecido) std::cout << "[INFO] Nenhum comando detectado dentro do tempo limite.\n";
            std::cout << "[INFO] Retornando ao modo de escuta da palavra-chave \"zenira\"...\n";
#endif
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
