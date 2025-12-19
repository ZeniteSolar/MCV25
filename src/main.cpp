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
#define LOGS_ERRO          0  // Habilita ou desabilita logs de erro e de aviso
#define LOGS_INFO          0  // Habilita ou desabilita logs informativos
#define VOSK_LOG_LEVEL     0  // Nível de log do Vosk (0: desativado, 1: erros, 2: avisos)
#define ENABLE_CAN         1  // Habilita ou desabilita o uso de CAN

/**
 * Função principal do programa.
 */
int main() {

#if LOGS_INFO
    std::cout << "[INFO] Inicializando Vosk e Audio...\n";
#endif

    vosk_set_log_level(VOSK_LOG_LEVEL);
    
    // Criar instância de Vosk
    Vosk vosk_handler;
    VoskModel* model = vosk_handler.load_vosk_model("vosk-models/vosk-model-small-pt-0.3");
    if (!model) {
#if LOGS_ERRO
        std::cerr << "[ERRO] Falha ao carregar modelo Vosk.\n";
#endif
        return 1;
    }

    // Criar instância de Audio
    Audio audio_handler;
    
    signal_t signal;
    snd_pcm_t* audio = audio_handler.audio_init();
    uint8_t listening_state[3];

    if (!audio) {
#if LOGS_ERRO
        std::cerr << "[ERRO] PCM não inicializado!\n";
#endif
        return 1;
    }

#if ENABLE_CAN
    // Criar instância de Can
    Can can_handler;
    int can_sock = can_handler.setup_can();
    if (can_sock < 0) {
#if LOGS_ERRO
        std::cerr << "[ERRO] Falha ao configurar interface CAN.\n";
#endif
        return 1;
    }
#else
    int can_sock = -1;
    Can can_handler;
#if LOGS_INFO
    std::cout << "[INFO] CAN desativado para testes locais.\n";
#endif
#endif

#if LOGS_INFO
    std::cout << "[INFO] Aguardando palavra de ativação: \"zenira\"...\n";
#endif

    while (true) {
        signal = audio_handler.get_audio(audio, audio_handler.get_raw_samples());  
        
        if (signal.total_length == 0 || signal.get_data == nullptr) {
            continue; // ou break para não tentar classificar
        }

        if (wake_word_detected(&signal)) {
            // Reproduzir som de confirmação de ativação
            audio_handler.run_audio("zenira_wakesound.wav");
            
#if ENABLE_CAN
            // Enviar mensagem CAN informando que o sistema está escutando
            listening_state[0] = CAN_SIGNATURE_MCV25;   // Byte 0: Signature
            listening_state[1] = 1;                     // Byte 1: State = 1 (escutando)
            listening_state[2] = 0;                     // Byte 2: Error = 0 (sem erro)
            can_handler.send_can(can_sock, CAN_MSG_MCV25_STATE_ID, listening_state, CAN_MSG_MCV25_STATE_LENGTH);
#if LOGS_INFO
            std::cout << "[INFO] Mensagem CAN enviada: Iniciando reconhecimento de comandos...\n";
#endif
#endif

#if LOGS_ERRO
            std::cout << "[INFO] Iniciando reconhecimento de comandos com Vosk...\n";
#endif
            VoskRecognizer* recognizer = vosk_handler.create_command_recognizer(model);
            bool comandoReconhecido = false;
            time_t inicio = time(nullptr);
            while (difftime(time(nullptr), inicio) < 5.0) {
                audio_handler.get_audio_raw(audio, audio_handler.get_raw_samples(), SAMPLE_LENGTH / 2);
                if (vosk_recognizer_accept_waveform(recognizer, (const char*)audio_handler.get_raw_samples()->data(), (SAMPLE_LENGTH / 2) * sizeof(int16_t))) {
                    std::string  result = vosk_handler.vosk_process(recognizer);
                    if (!result.empty()) {
                        std::string comando = result;
#if LOGS_INFO
                        std::cout << "[INFO] Detectado: \"" << comando << "\"\n";
#endif

#if ENABLE_CAN
                        can_handler.execute_commands(comando, can_sock);
#endif
                        
                        // Reproduzir som de confirmação de comando executado (segundo áudio)
                        audio_handler.run_audio("command_executed.wav");
                        
                        comandoReconhecido = true;
                        break;
                    }
                }
            }

            // Reproduzir som de confirmação encerramento 
            audio_handler.run_audio("zenira_endpoint.wav");
            
#if ENABLE_CAN
            // Enviar mensagem CAN informando que o sistema não está mais escutando
            listening_state[0] = CAN_SIGNATURE_MCV25;   // Byte 0: Signature
            listening_state[1] = 0;                     // Byte 1: State = 0 (não escutando)
            listening_state[2] = 0;                     // Byte 2: Error = 0 (sem erro)
            can_handler.send_can(can_sock, CAN_MSG_MCV25_STATE_ID, listening_state, CAN_MSG_MCV25_STATE_LENGTH);
#if LOGS_INFO
            std::cout << "[INFO] Mensagem CAN enviada: Sistema parou de escutar.\n";
#endif
#endif

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
    can_handler.close_can(can_sock);
#endif

    return 0;
}
